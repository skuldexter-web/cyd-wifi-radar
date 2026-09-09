// ============================================================================
// config.h — Hardware pins, CSI tuning, and display constants
// CYD (ESP32-2432S028R) Wi-Fi CSI Radar
// ============================================================================
#pragma once
#include <cstdint>

// ---------------------------------------------------------------------------
// Display pins (also mirrored in platformio.ini build_flags for TFT_eSPI —
// kept here too as the single source of truth for anything outside TFT_eSPI,
// e.g. the backlight PWM channel setup in gui_manager.cpp)
// ---------------------------------------------------------------------------
namespace pins {
    constexpr int8_t TFT_MOSI = 13;
    constexpr int8_t TFT_MISO = 12;
    constexpr int8_t TFT_SCLK = 14;
    constexpr int8_t TFT_CS   = 15;
    constexpr int8_t TFT_DC   = 2;
    constexpr int8_t TFT_RST  = -1;   // tied to EN on most CYD boards
    constexpr int8_t TFT_BL   = 21;   // PWM backlight
}

// ---------------------------------------------------------------------------
// Backlight PWM
// ---------------------------------------------------------------------------
namespace backlight {
    constexpr uint8_t  PWM_CHANNEL   = 0;
    constexpr uint32_t PWM_FREQ_HZ   = 5000;
    constexpr uint8_t  PWM_RES_BITS  = 8;      // 0-255
    constexpr uint8_t  DEFAULT_DUTY  = 200;    // ~78% brightness
}

// ---------------------------------------------------------------------------
// Wi-Fi / CSI capture
//
// ASSUMPTION (flagged per user decision): passive promiscuous-mode capture.
// No AP association, no active traffic generation. CSI callback frequency
// is entirely dependent on ambient RF activity on WIFI_CHANNEL — beacons,
// probe requests/responses, and any data frames from nearby devices.
// On a quiet channel this can be sparse (sub-1 Hz). This is a hard physical
// limitation of the passive approach, not a bug in this code.
// ---------------------------------------------------------------------------
namespace csi_cfg {
    constexpr uint8_t  WIFI_CHANNEL          = 6;     // fixed listen channel (1-13)
    constexpr uint32_t CALIBRATION_MS        = 5000;  // auto-baseline window at boot
    constexpr size_t   SUBCARRIER_COUNT_HT20 = 64;    // ESP32 HT20 CSI buffer length
    constexpr size_t   VARIANCE_WINDOW       = 32;    // samples in moving-variance ring buffer
    constexpr uint32_t STALE_DATA_TIMEOUT_MS = 3000;  // no CSI packets -> show "NO SIGNAL"

    // Motion Index thresholds (0-100 normalized scale)
    constexpr float MOTION_THRESHOLD_LOW   = 15.0f;   // below = IDLE
    constexpr float MOTION_THRESHOLD_HIGH  = 45.0f;   // above = MOTION DETECTED (vs CALIBRATING/IDLE band)

    // Exponential moving average smoothing factor for displayed motion index
    // (raw variance is noisy; this trades responsiveness for readability)
    constexpr float EMA_ALPHA = 0.25f;
}

// ---------------------------------------------------------------------------
// FreeRTOS task configuration
// ---------------------------------------------------------------------------
namespace tasks {
    constexpr uint32_t CSI_TASK_STACK_BYTES = 4096;
    constexpr uint32_t GUI_TASK_STACK_BYTES = 8192;
    constexpr UBaseType_t CSI_TASK_PRIORITY = 2;
    constexpr UBaseType_t GUI_TASK_PRIORITY = 1;
    constexpr BaseType_t  CSI_TASK_CORE     = 0;
    constexpr BaseType_t  GUI_TASK_CORE     = 1;

    constexpr uint32_t GUI_FRAME_INTERVAL_MS = 33; // ~30 FPS render target
}

// ---------------------------------------------------------------------------
// Display / theme
// ---------------------------------------------------------------------------
namespace theme {
    constexpr uint16_t BG_COLOR       = 0x0000; // black
    constexpr uint16_t PANEL_COLOR    = 0x10A2; // very dark navy
    constexpr uint16_t GRID_COLOR     = 0x2965; // dim slate
    constexpr uint16_t TEXT_PRIMARY   = 0xFFFF; // white
    constexpr uint16_t TEXT_DIM       = 0x8C71; // grey
    constexpr uint16_t COLOR_CALM     = 0x07FF; // cyan
    constexpr uint16_t COLOR_MID      = 0xFEA0; // amber
    constexpr uint16_t COLOR_HIGH     = 0xF800; // red
    constexpr uint16_t COLOR_IDLE_TXT = 0x07E0; // green
}

enum class SystemStatus : uint8_t {
    CALIBRATING = 0,
    IDLE        = 1,
    MOTION      = 2,
    NO_SIGNAL   = 3
};
