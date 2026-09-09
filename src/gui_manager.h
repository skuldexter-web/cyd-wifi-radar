// ============================================================================
// gui_manager.h — Display driver, sprite-based double buffering, radar UI
//
// Runs on Core 1. Reads CsiSnapshot via CsiAnalyzer::getSnapshot() each
// frame and renders the full dashboard into an off-screen TFT_eSPI sprite,
// then pushes that sprite to the panel in one SPI burst — this is what
// eliminates flicker vs. drawing primitives directly to the display.
//
// Memory note: 320x240 @ 16bpp = 153,600 bytes for a full-frame sprite.
// The ESP32-D0WDQ6 has 520KB SRAM total; a full-frame sprite is feasible
// but leaves little headroom alongside Wi-Fi/CSI buffers and FreeRTOS task
// stacks. This implementation uses a full-frame sprite for simplicity and
// because CSI (not graphics) is the RAM-hungrier subsystem here; if you
// later add LVGL widgets or increase CSI buffer depth, switch to a
// row-banded partial sprite (see comment at the bottom of this file).
// ============================================================================
#pragma once
#include <TFT_eSPI.h>
#include "config.h"
#include "csi_analyzer.h"

class GuiManager {
public:
    void begin();

    // Renders one full frame using the given snapshot. Call at
    // tasks::GUI_FRAME_INTERVAL_MS cadence from the GUI task.
    void renderFrame(const CsiSnapshot& snap);

    static void taskEntry(void* pvParameters);
    void attachCsiAnalyzer(CsiAnalyzer* analyzer) { m_csi = analyzer; }

private:
    void drawStatusBar(const CsiSnapshot& snap);
    void drawRadar(const CsiSnapshot& snap);
    void drawAnalyticsBar(const CsiSnapshot& snap);

    uint16_t statusColor(SystemStatus s) const;
    const char* statusText(SystemStatus s) const;
    uint16_t motionColor(float motionIndex) const;

    TFT_eSPI    m_tft;
    TFT_eSprite m_frame{&m_tft};

    CsiAnalyzer* m_csi = nullptr;

    // Radar sweep animation state
    float    m_sweepAngleDeg = 0.0f;
    uint32_t m_lastFrameMs   = 0;

    // Expanding-pulse ring animation state (triggered on MOTION status)
    static constexpr uint8_t MAX_PULSES = 4;
    struct Pulse { float radius; bool active; };
    Pulse    m_pulses[MAX_PULSES] = {};
    uint32_t m_lastPulseSpawnMs = 0;

    // Layout constants (derived from 320x240 panel, landscape orientation)
    static constexpr int16_t SCREEN_W = 320;
    static constexpr int16_t SCREEN_H = 240;
    static constexpr int16_t STATUS_BAR_H = 28;
    static constexpr int16_t ANALYTICS_BAR_H = 40;
    static constexpr int16_t RADAR_CENTER_X = SCREEN_W / 2;
    static constexpr int16_t RADAR_CENTER_Y = STATUS_BAR_H + (SCREEN_H - STATUS_BAR_H - ANALYTICS_BAR_H) / 2;
    static constexpr int16_t RADAR_RADIUS   = 85;
};

// ----------------------------------------------------------------------------
// Row-banded partial sprite alternative (not used by default — documented
// for future tuning): allocate m_frame at SCREEN_W x 60px, render+push in
// 4 horizontal bands per frame. Cuts sprite RAM from 153.6KB to ~28.8KB at
// the cost of more SPI push calls per frame. Swap in if CSI buffer growth
// or LVGL adoption creates memory pressure.
// ----------------------------------------------------------------------------
