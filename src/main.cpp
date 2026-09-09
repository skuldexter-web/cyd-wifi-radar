// ============================================================================
// main.cpp — CYD Wi-Fi CSI Radar
//
// System architecture:
//   Core 0: CsiAnalyzer task — Wi-Fi promiscuous capture + motion index math
//   Core 1: GuiManager task  — TFT_eSPI sprite rendering @ ~30 FPS
//   Arduino loop() on Core 1 is left essentially idle; all real work is in
//   the two pinned tasks so neither the Wi-Fi driver nor SPI rendering ever
//   blocks on the other.
// ============================================================================
#include <Arduino.h>
#include "config.h"
#include "csi_analyzer.h"
#include "gui_manager.h"

static CsiAnalyzer g_csiAnalyzer;
static GuiManager  g_guiManager;

static TaskHandle_t g_csiTaskHandle = nullptr;
static TaskHandle_t g_guiTaskHandle = nullptr;

void setup() {
    Serial.begin(115200);
    delay(200); // let USB-serial settle on boards where CDC needs a moment
    Serial.println();
    Serial.println("=========================================");
    Serial.println(" CYD Wi-Fi CSI Radar — booting");
    Serial.println("=========================================");

    g_guiManager.attachCsiAnalyzer(&g_csiAnalyzer);

    bool csiOk = g_csiAnalyzer.begin();
    if (!csiOk) {
        // Do not halt: the display can still boot and show NO_SIGNAL/error
        // state, which is more useful in the field than a dead board.
        Serial.println("[MAIN] CSI init failed — continuing so the display "
                        "can at least show a diagnostic state.");
    }

    BaseType_t csiTaskResult = xTaskCreatePinnedToCore(
        CsiAnalyzer::taskEntry,
        "CSI_Task",
        tasks::CSI_TASK_STACK_BYTES,
        &g_csiAnalyzer,
        tasks::CSI_TASK_PRIORITY,
        &g_csiTaskHandle,
        tasks::CSI_TASK_CORE
    );
    if (csiTaskResult != pdPASS) {
        Serial.println("[MAIN] FATAL: failed to create CSI task (heap exhausted?).");
    }

    BaseType_t guiTaskResult = xTaskCreatePinnedToCore(
        GuiManager::taskEntry,
        "GUI_Task",
        tasks::GUI_TASK_STACK_BYTES,
        &g_guiManager,
        tasks::GUI_TASK_PRIORITY,
        &g_guiTaskHandle,
        tasks::GUI_TASK_CORE
    );
    if (guiTaskResult != pdPASS) {
        Serial.println("[MAIN] FATAL: failed to create GUI task (heap exhausted?).");
    }

    Serial.println("[MAIN] Tasks launched. CSI on Core 0, GUI on Core 1.");
}

void loop() {
    // Intentionally idle — all work happens in the pinned FreeRTOS tasks.
    // A slow heartbeat here is cheap insurance for future watchdog/health
    // logging without disturbing the two real-time tasks.
    vTaskDelay(pdMS_TO_TICKS(1000));
}
