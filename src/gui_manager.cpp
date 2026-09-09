// ============================================================================
// gui_manager.cpp
// ============================================================================
#include "gui_manager.h"
#include <cmath>

void GuiManager::begin() {
    // Backlight PWM setup — done here rather than TFT_eSPI (which only
    // handles the SPI display lines) since BL is a bare PWM-capable GPIO.
    ledcSetup(backlight::PWM_CHANNEL, backlight::PWM_FREQ_HZ, backlight::PWM_RES_BITS);
    ledcAttachPin(pins::TFT_BL, backlight::PWM_CHANNEL);
    ledcWrite(backlight::PWM_CHANNEL, backlight::DEFAULT_DUTY);

    m_tft.init();
    m_tft.setRotation(1); // landscape, USB-left orientation typical for CYD
    m_tft.fillScreen(theme::BG_COLOR);

    m_frame.setColorDepth(16);
    bool ok = m_frame.createSprite(SCREEN_W, SCREEN_H);
    if (!ok) {
        // Sprite allocation failed (out of heap). Fall back to direct
        // drawing would require duplicating every draw call against m_tft
        // instead of m_frame — flagged rather than silently degraded, since
        // that fallback isn't implemented here and flicker WILL be visible.
        Serial.println("[GUI] FATAL: sprite allocation failed (heap exhausted). "
                        "Rendering will not proceed correctly.");
    }
    m_frame.setTextDatum(TL_DATUM);

    m_lastFrameMs = millis();
}

uint16_t GuiManager::statusColor(SystemStatus s) const {
    switch (s) {
        case SystemStatus::CALIBRATING: return theme::COLOR_MID;
        case SystemStatus::IDLE:        return theme::COLOR_IDLE_TXT;
        case SystemStatus::MOTION:      return theme::COLOR_HIGH;
        case SystemStatus::NO_SIGNAL:   return theme::TEXT_DIM;
    }
    return theme::TEXT_PRIMARY;
}

const char* GuiManager::statusText(SystemStatus s) const {
    switch (s) {
        case SystemStatus::CALIBRATING: return "CALIBRATING";
        case SystemStatus::IDLE:        return "IDLE";
        case SystemStatus::MOTION:      return "MOTION DETECTED";
        case SystemStatus::NO_SIGNAL:   return "NO SIGNAL";
    }
    return "UNKNOWN";
}

uint16_t GuiManager::motionColor(float motionIndex) const {
    // Cool cyan (calm) -> amber (mid) -> red (high), interpolated.
    if (motionIndex < csi_cfg::MOTION_THRESHOLD_LOW) {
        return theme::COLOR_CALM;
    } else if (motionIndex < csi_cfg::MOTION_THRESHOLD_HIGH) {
        return theme::COLOR_MID;
    }
    return theme::COLOR_HIGH;
}

void GuiManager::drawStatusBar(const CsiSnapshot& snap) {
    m_frame.fillRect(0, 0, SCREEN_W, STATUS_BAR_H, theme::PANEL_COLOR);
    m_frame.drawFastHLine(0, STATUS_BAR_H, SCREEN_W, theme::GRID_COLOR);

    m_frame.setTextColor(theme::TEXT_DIM, theme::PANEL_COLOR);
    m_frame.setTextSize(1);
    m_frame.drawString("CH " + String(snap.channel), 6, 9);
    m_frame.drawString("RSSI " + String(snap.rssi) + "dBm", 60, 9);

    uint16_t sc = statusColor(snap.status);
    const char* txt = statusText(snap.status);
    m_frame.setTextColor(sc, theme::PANEL_COLOR);
    int16_t textW = m_frame.textWidth(txt);
    m_frame.drawString(txt, SCREEN_W - textW - 8, 9);

    // Status LED dot
    m_frame.fillCircle(SCREEN_W - textW - 18, 14, 4, sc);
}

void GuiManager::drawRadar(const CsiSnapshot& snap) {
    // Sweep speed scales with motion index: faster sweep reads as "more
    // active" without needing extra widgets. Idle sweep is slow and calm.
    float degPerFrame = 1.5f + (snap.motionIndex / 100.0f) * 4.5f;
    m_sweepAngleDeg = fmodf(m_sweepAngleDeg + degPerFrame, 360.0f);

    uint16_t baseColor = motionColor(snap.motionIndex);

    // Concentric grid rings
    for (int r = RADAR_RADIUS; r > 0; r -= RADAR_RADIUS / 3) {
        m_frame.drawCircle(RADAR_CENTER_X, RADAR_CENTER_Y, r, theme::GRID_COLOR);
    }
    // Crosshair
    m_frame.drawFastHLine(RADAR_CENTER_X - RADAR_RADIUS, RADAR_CENTER_Y, RADAR_RADIUS * 2, theme::GRID_COLOR);
    m_frame.drawFastVLine(RADAR_CENTER_X, RADAR_CENTER_Y - RADAR_RADIUS, RADAR_RADIUS * 2, theme::GRID_COLOR);

    // Sweeping arc (fading trail effect via 3 successive angles)
    for (int i = 0; i < 3; ++i) {
        float ang = m_sweepAngleDeg - (i * 8.0f);
        float rad = ang * (PI / 180.0f);
        int16_t x = RADAR_CENTER_X + static_cast<int16_t>(cosf(rad) * RADAR_RADIUS);
        int16_t y = RADAR_CENTER_Y + static_cast<int16_t>(sinf(rad) * RADAR_RADIUS);
        uint8_t alpha = 255 - (i * 70); // simulated fade via color scale below
        uint16_t fadedColor = m_frame.alphaBlend(alpha, baseColor, theme::BG_COLOR);
        m_frame.drawLine(RADAR_CENTER_X, RADAR_CENTER_Y, x, y, fadedColor);
    }

    // Expanding pulse rings — spawn a new one periodically while in MOTION
    uint32_t now = millis();
    if (snap.status == SystemStatus::MOTION && (now - m_lastPulseSpawnMs > 600)) {
        for (auto& p : m_pulses) {
            if (!p.active) {
                p.active = true;
                p.radius = 4.0f;
                break;
            }
        }
        m_lastPulseSpawnMs = now;
    }
    for (auto& p : m_pulses) {
        if (!p.active) continue;
        p.radius += 2.2f;
        if (p.radius > RADAR_RADIUS) {
            p.active = false;
            continue;
        }
        uint8_t fade = static_cast<uint8_t>(255.0f * (1.0f - (p.radius / RADAR_RADIUS)));
        uint16_t pulseColor = m_frame.alphaBlend(fade, theme::COLOR_HIGH, theme::BG_COLOR);
        m_frame.drawCircle(RADAR_CENTER_X, RADAR_CENTER_Y, static_cast<int16_t>(p.radius), pulseColor);
    }

    // Center motion index readout
    m_frame.setTextDatum(MC_DATUM);
    m_frame.setTextColor(baseColor, theme::BG_COLOR);
    m_frame.setTextSize(1);
    char buf[8];
    snprintf(buf, sizeof(buf), "%.0f%%", snap.motionIndex);
    m_frame.drawString(buf, RADAR_CENTER_X, RADAR_CENTER_Y);
    m_frame.setTextDatum(TL_DATUM);
}

void GuiManager::drawAnalyticsBar(const CsiSnapshot& snap) {
    int16_t barY = SCREEN_H - ANALYTICS_BAR_H;
    m_frame.fillRect(0, barY, SCREEN_W, ANALYTICS_BAR_H, theme::PANEL_COLOR);
    m_frame.drawFastHLine(0, barY, SCREEN_W, theme::GRID_COLOR);

    // Gauge bar
    int16_t gaugeX = 10, gaugeY = barY + 8, gaugeW = 220, gaugeH = 14;
    m_frame.drawRect(gaugeX, gaugeY, gaugeW, gaugeH, theme::GRID_COLOR);
    int16_t fillW = static_cast<int16_t>((snap.motionIndex / 100.0f) * (gaugeW - 2));
    uint16_t gaugeColor = motionColor(snap.motionIndex);
    if (fillW > 0) {
        m_frame.fillRect(gaugeX + 1, gaugeY + 1, fillW, gaugeH - 2, gaugeColor);
    }

    m_frame.setTextColor(theme::TEXT_PRIMARY, theme::PANEL_COLOR);
    m_frame.setTextSize(1);
    char buf[24];
    snprintf(buf, sizeof(buf), "MOTION %.0f%%", snap.motionIndex);
    m_frame.drawString(buf, gaugeX, barY + 24);

    snprintf(buf, sizeof(buf), "VAR %.2f", snap.rawVariance);
    m_frame.setTextColor(theme::TEXT_DIM, theme::PANEL_COLOR);
    m_frame.drawString(buf, gaugeX + gaugeW + 10, barY + 8);

    snprintf(buf, sizeof(buf), "PKT %lu", static_cast<unsigned long>(snap.packetsReceived));
    m_frame.drawString(buf, gaugeX + gaugeW + 10, barY + 24);
}

void GuiManager::renderFrame(const CsiSnapshot& snap) {
    m_frame.fillSprite(theme::BG_COLOR);
    drawStatusBar(snap);
    drawRadar(snap);
    drawAnalyticsBar(snap);
    m_frame.pushSprite(0, 0); // single SPI burst -> flicker-free swap
}

void GuiManager::taskEntry(void* pvParameters) {
    auto* self = static_cast<GuiManager*>(pvParameters);
    self->begin();

    TickType_t lastWake = xTaskGetTickCount();
    for (;;) {
        CsiSnapshot snap = self->m_csi ? self->m_csi->getSnapshot() : CsiSnapshot{};
        self->renderFrame(snap);
        vTaskDelayUntil(&lastWake, pdMS_TO_TICKS(tasks::GUI_FRAME_INTERVAL_MS));
    }
}
