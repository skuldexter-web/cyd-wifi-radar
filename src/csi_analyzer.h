// ============================================================================
// csi_analyzer.h — Wi-Fi CSI capture + motion index calculation
//
// Runs on Core 0. Publishes results through a lock-free snapshot struct
// guarded by a spinlock (portMUX), since updates are frequent but tiny
// (a handful of floats/ints) — a full queue is unnecessary overhead here
// and would only add latency for the GUI's read-every-frame access pattern.
// ============================================================================
#pragma once
#include <Arduino.h>
#include <esp_wifi.h>       // wifi_csi_info_t, wifi_csi_config_t, esp_wifi_set_csi_*
#include <esp_wifi_types.h> // wifi_promiscuous_pkt_type_t, wifi_promiscuous_pkt_t
#include "config.h"

struct CsiSnapshot {
    float          motionIndex     = 0.0f;   // 0-100, EMA-smoothed
    float          rawVariance     = 0.0f;   // last raw variance sample
    int8_t         rssi            = 0;
    uint8_t        channel         = csi_cfg::WIFI_CHANNEL;
    SystemStatus   status          = SystemStatus::CALIBRATING;
    uint32_t       packetsReceived = 0;
    uint32_t       lastPacketMs    = 0;
    bool           calibrated      = false;
};

class CsiAnalyzer {
public:
    // Starts promiscuous-mode Wi-Fi capture and the CSI subsystem.
    // Must be called once from setup() before the CSI task is spun up.
    // Returns false on any esp_wifi_* init failure (check Serial log for cause).
    bool begin();

    // Thread-safe copy of the latest computed values. Cheap; call every frame.
    CsiSnapshot getSnapshot() const;

    // FreeRTOS task entry point (pinned to Core 0 from main.cpp).
    // Owns the calibration timer and periodic staleness checks; the actual
    // per-packet math happens in the static ISR-context callback.
    static void taskEntry(void* pvParameters);

private:
    void loop();
    void checkStaleness();

    static CsiAnalyzer* s_instance; // needed so the C-style esp_wifi callback
                                     // can reach instance state
    static void IRAM_ATTR onCsiRx(void* ctx, wifi_csi_info_t* data);
    static void onWifiPromiscuousRx(void* buf, wifi_promiscuous_pkt_type_t type);

    void processCsiPacket(const wifi_csi_info_t* data);

    mutable portMUX_TYPE m_mux = portMUX_INITIALIZER_UNLOCKED;
    CsiSnapshot m_snapshot;

    // Ring buffer for moving-variance calculation across recent amplitude
    // samples (mean subcarrier amplitude per packet, not per-subcarrier —
    // per-subcarrier variance-of-variance is unnecessary for a presence/
    // motion signal and would burn Core 0 cycles we don't have to spare
    // alongside promiscuous-mode packet handling).
    float    m_ampHistory[csi_cfg::VARIANCE_WINDOW] = {0};
    size_t   m_ampHistoryIdx = 0;
    size_t   m_ampHistoryFilled = 0;

    uint32_t m_bootMs = 0;
};
