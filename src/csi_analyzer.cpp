// ============================================================================
// csi_analyzer.cpp
// ============================================================================
#include "csi_analyzer.h"
#include <WiFi.h>
#include <esp_wifi.h>

CsiAnalyzer* CsiAnalyzer::s_instance = nullptr;

// ---------------------------------------------------------------------------
// begin()
//
// NOTE ON APPROACH: This project uses PASSIVE promiscuous-mode capture —
// no AP association, no DHCP, no active traffic generation (per explicit
// project decision). esp_wifi_set_csi_config()/esp_wifi_set_csi_rx_cb()
// still function without an association because the radio is simply
// listening on WIFI_CHANNEL and computing CSI for any 802.11 frame it can
// decode, including management frames (beacons/probe requests) when
// csi_config.mgmt is enabled below. This is what makes passive mode viable
// at all — pure data-frame-only CSI would nearly starve on an idle channel.
//
// Trade-off vs. active-ping CSI: packet arrival is bursty and depends on
// ambient traffic you do not control, so the motion index update rate is
// inherently variable. See STALE_DATA_TIMEOUT_MS in config.h.
// ---------------------------------------------------------------------------
bool CsiAnalyzer::begin() {
    s_instance = this;
    m_bootMs = millis();
    m_snapshot.status = SystemStatus::CALIBRATING;
    m_snapshot.channel = csi_cfg::WIFI_CHANNEL;

    WiFi.mode(WIFI_MODE_STA);
    WiFi.disconnect(); // ensure no association attempt; passive listen only

    esp_err_t err;

    err = esp_wifi_set_promiscuous(true);
    if (err != ESP_OK) {
        Serial.printf("[CSI] esp_wifi_set_promiscuous failed: %d\n", err);
        return false;
    }

    err = esp_wifi_set_channel(csi_cfg::WIFI_CHANNEL, WIFI_SECOND_CHAN_NONE);
    if (err != ESP_OK) {
        Serial.printf("[CSI] esp_wifi_set_channel failed: %d\n", err);
        return false;
    }

    // Promiscuous sniff callback — used only to count/observe raw traffic
    // for the RSSI readout; CSI math itself happens in onCsiRx below.
    esp_wifi_set_promiscuous_rx_cb(&CsiAnalyzer::onWifiPromiscuousRx);

    wifi_csi_config_t csi_config = {};
    csi_config.lltf_en           = true;
    csi_config.htltf_en          = true;
    csi_config.stbc_htltf2_en    = true;
    csi_config.ltf_merge_en      = true;
    csi_config.channel_filter_en = false; // accept frames from any BSSID (passive/promiscuous intent)
    csi_config.manu_scale        = false;

    err = esp_wifi_set_csi_config(&csi_config);
    if (err != ESP_OK) {
        Serial.printf("[CSI] esp_wifi_set_csi_config failed: %d\n", err);
        return false;
    }

    err = esp_wifi_set_csi_rx_cb(&CsiAnalyzer::onCsiRx, this);
    if (err != ESP_OK) {
        Serial.printf("[CSI] esp_wifi_set_csi_rx_cb failed: %d\n", err);
        return false;
    }

    err = esp_wifi_set_csi(true);
    if (err != ESP_OK) {
        Serial.printf("[CSI] esp_wifi_set_csi(true) failed: %d\n", err);
        return false;
    }

    Serial.println("[CSI] Passive promiscuous CSI capture started.");
    Serial.printf("[CSI] Listening on channel %d (no AP association).\n", csi_cfg::WIFI_CHANNEL);
    return true;
}

CsiSnapshot CsiAnalyzer::getSnapshot() const {
    portENTER_CRITICAL(&m_mux);
    CsiSnapshot copy = m_snapshot;
    portEXIT_CRITICAL(&m_mux);
    return copy;
}

// ---------------------------------------------------------------------------
// onWifiPromiscuousRx — fires for every captured 802.11 frame (not just
// ones that yield CSI). Used here only to keep a live RSSI reading, since
// with no AP association there is no "connection RSSI" from WiFi.RSSI().
// ---------------------------------------------------------------------------
void CsiAnalyzer::onWifiPromiscuousRx(void* buf, wifi_promiscuous_pkt_type_t type) {
    if (!s_instance || type != WIFI_PKT_MGMT) return;
    auto* pkt = static_cast<wifi_promiscuous_pkt_t*>(buf);

    portENTER_CRITICAL(&s_instance->m_mux);
    s_instance->m_snapshot.rssi = pkt->rx_ctrl.rssi;
    portEXIT_CRITICAL(&s_instance->m_mux);
}

// ---------------------------------------------------------------------------
// onCsiRx — ISR-adjacent context (called from the Wi-Fi driver task, not a
// true hardware ISR, but treat it as latency-critical: no heap allocation,
// no blocking calls, no Serial prints in the hot path).
// ---------------------------------------------------------------------------
void IRAM_ATTR CsiAnalyzer::onCsiRx(void* ctx, wifi_csi_info_t* data) {
    auto* self = static_cast<CsiAnalyzer*>(ctx);
    if (!self || !data || !data->buf || data->len == 0) return;
    self->processCsiPacket(data);
}

void CsiAnalyzer::processCsiPacket(const wifi_csi_info_t* data) {
    // CSI buffer is interleaved int8 pairs: [imag, real] per subcarrier.
    const int8_t* raw = data->buf;
    const size_t  pairCount = data->len / 2;
    if (pairCount == 0) return;

    // Mean subcarrier amplitude for this packet — a compact, cheap
    // presence-sensitive scalar. Per-subcarrier tracking would give finer
    // spatial resolution but isn't needed for a single aggregate motion
    // index, and would cost more RAM/CPU than this passive-mode budget allows.
    float sumAmp = 0.0f;
    for (size_t i = 0; i < pairCount; ++i) {
        int8_t imag = raw[2 * i];
        int8_t real = raw[2 * i + 1];
        sumAmp += sqrtf(static_cast<float>(imag) * imag + static_cast<float>(real) * real);
    }
    float meanAmp = sumAmp / static_cast<float>(pairCount);

    // Push into ring buffer
    m_ampHistory[m_ampHistoryIdx] = meanAmp;
    m_ampHistoryIdx = (m_ampHistoryIdx + 1) % csi_cfg::VARIANCE_WINDOW;
    if (m_ampHistoryFilled < csi_cfg::VARIANCE_WINDOW) m_ampHistoryFilled++;

    // Moving variance across the ring buffer (high-pass proxy: static
    // multipath -> low variance; body movement disturbing reflections ->
    // higher variance). Requires at least a few samples to be meaningful.
    float variance = 0.0f;
    if (m_ampHistoryFilled >= 4) {
        float mean = 0.0f;
        for (size_t i = 0; i < m_ampHistoryFilled; ++i) mean += m_ampHistory[i];
        mean /= static_cast<float>(m_ampHistoryFilled);

        float sqSum = 0.0f;
        for (size_t i = 0; i < m_ampHistoryFilled; ++i) {
            float d = m_ampHistory[i] - mean;
            sqSum += d * d;
        }
        variance = sqSum / static_cast<float>(m_ampHistoryFilled);
    }

    // Normalize variance to a 0-100 motion index. The divisor below is an
    // empirical scaling constant, not derived from a spec — CSI amplitude
    // variance magnitude depends heavily on antenna, room, and ambient
    // traffic mix, so this WILL need on-site tuning. [Guessing / needs
    // field calibration] Exposed here as a single constant for that reason.
    constexpr float VARIANCE_SCALE = 40.0f;
    float normalized = (variance / VARIANCE_SCALE) * 100.0f;
    if (normalized > 100.0f) normalized = 100.0f;
    if (normalized < 0.0f) normalized = 0.0f;

    portENTER_CRITICAL(&m_mux);
    m_snapshot.rawVariance = variance;
    m_snapshot.motionIndex = (csi_cfg::EMA_ALPHA * normalized) +
                             ((1.0f - csi_cfg::EMA_ALPHA) * m_snapshot.motionIndex);
    m_snapshot.packetsReceived++;
    m_snapshot.lastPacketMs = millis();

    if (m_snapshot.calibrated) {
        if (m_snapshot.motionIndex >= csi_cfg::MOTION_THRESHOLD_HIGH) {
            m_snapshot.status = SystemStatus::MOTION;
        } else if (m_snapshot.motionIndex < csi_cfg::MOTION_THRESHOLD_LOW) {
            m_snapshot.status = SystemStatus::IDLE;
        }
        // between thresholds: leave status as-is (hysteresis band, avoids flicker)
    }
    portEXIT_CRITICAL(&m_mux);
}

void CsiAnalyzer::checkStaleness() {
    uint32_t now = millis();
    portENTER_CRITICAL(&m_mux);
    bool stale = (m_snapshot.packetsReceived > 0) &&
                 (now - m_snapshot.lastPacketMs > csi_cfg::STALE_DATA_TIMEOUT_MS);
    if (stale) {
        m_snapshot.status = SystemStatus::NO_SIGNAL;
    }
    portEXIT_CRITICAL(&m_mux);
}

void CsiAnalyzer::loop() {
    uint32_t elapsed = millis() - m_bootMs;

    if (!m_snapshot.calibrated && elapsed >= csi_cfg::CALIBRATION_MS) {
        portENTER_CRITICAL(&m_mux);
        m_snapshot.calibrated = true;
        m_snapshot.status = SystemStatus::IDLE;
        portEXIT_CRITICAL(&m_mux);
        Serial.println("[CSI] Calibration window complete.");
    }

    checkStaleness();
    vTaskDelay(pdMS_TO_TICKS(100));
}

void CsiAnalyzer::taskEntry(void* pvParameters) {
    auto* self = static_cast<CsiAnalyzer*>(pvParameters);
    for (;;) {
        self->loop();
    }
}
