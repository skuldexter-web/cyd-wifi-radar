# CYD Wi-Fi CSI Radar

Passive Wi-Fi CSI motion/presence visualizer for the ESP32-2432S028R ("CYD").

## Status of this deliverable — read before flashing

This zip contains **complete, real source code** — not a template. It does
**not** contain compiled `.bin` files. Building this project requires
Espressif's Xtensa toolchain and the ESP32 Arduino core, which PlatformIO
fetches from its package registry at build time; that registry was not
reachable from the sandbox this was built in, so no build was performed here.
Anything claiming otherwise would be fabricated — I'd rather you get real
source and build it yourself than binaries that were never actually compiled.

## Build & flash (do this on your own machine)

```bash
pip install platformio
cd CYD_WiFi_Radar
pio run                 # builds — fetches toolchain + libs on first run
./flash.sh /dev/ttyUSB0 # or flash.bat COM5 on Windows
```

`flash.sh` / `flash.bat` read directly from `.pio/build/esp32dev/`, so once
`pio run` succeeds, flashing is one command.

## Design decisions baked into this code (read these)

1. **Passive promiscuous-mode CSI, no AP association** — this was an
   explicit project choice, not a default. It means CSI callback frequency
   depends entirely on ambient Wi-Fi traffic on the fixed listen channel
   (`csi_cfg::WIFI_CHANNEL` in `config.h`, default 6). On a quiet channel
   with few nearby devices, motion updates will be sparse and laggy — that
   is a physical limitation of passive sensing, not a bug. If you want
   faster, denser CSI, switch to active-ping mode (associate to an AP,
   ping the gateway every ~50-100ms) — that requires reworking
   `csi_analyzer.cpp`'s `begin()`.

2. **`VARIANCE_SCALE` in `csi_analyzer.cpp` is an unvalidated placeholder.**
   Real CSI amplitude variance magnitude depends on your antenna, room
   geometry, and ambient traffic mix. Flash this, watch the raw `VAR`
   readout in the analytics bar while you walk in and out of the room, and
   adjust `VARIANCE_SCALE` and the two thresholds in `config.h`
   (`MOTION_THRESHOLD_LOW/HIGH`) to match what you actually observe. Do not
   trust the default motion percentages out of the box.

3. **Full-frame sprite (153.6KB)** is used for the double buffer, simplest
   architecture. If you later add CSI buffer depth or LVGL widgets and hit
   heap exhaustion (`GuiManager::begin()` logs this explicitly rather than
   failing silently), switch to the row-banded partial-sprite approach
   documented at the bottom of `gui_manager.h`.

## Hardware pin reference

| Function | GPIO |
|---|---|
| TFT MOSI | 13 |
| TFT MISO | 12 |
| TFT SCLK | 14 |
| TFT CS   | 15 |
| TFT DC   | 2  |
| TFT RST  | -1 (tied to EN) |
| TFT BL   | 21 (PWM) |

Touch (XPT2046) is wired on the CYD but unused by this application.

## Changelog

**v2 (this version):** Fixes three real build errors reported from a local
`pio run`:
1. `config.h`'s `pins` namespace declared `TFT_MOSI`/`TFT_CS`/`TFT_DC`/`TFT_RST`/`TFT_BL`,
   which collided with the identically-named global macros TFT_eSPI needs
   from `platformio.ini`'s `build_flags` — the preprocessor doesn't respect
   namespaces, so `pins::TFT_BL` expanded to `pins::21`. Renamed to
   `pins::BL_PIN` (the only one actually needed outside TFT_eSPI); the rest
   are left owned entirely by the build-flag macros.
2. `csi_analyzer.h` used `wifi_csi_info_t` / `wifi_promiscuous_pkt_type_t`
   without including the ESP-IDF headers that declare them
   (`esp_wifi.h`, `esp_wifi_types.h`).
3. `platformio.ini` pinned `espressif32@6.7.0`, which the PlatformIO
   registry currently resolves to an incompatible `framework-arduinoespressif32@3.20016.0`
   (Arduino core 3.x) against a GCC 8.4.0 toolchain built for core 2.x —
   causing `typeof()`/statement-expression errors deep in ESP-IDF's
   `gpio_ll.h`. Repinned to `6.9.0` (Arduino core 2.0.17), the commonly
   reported working pairing for this toolchain.

**Caveat on this fix**: this sandbox cannot reach `api.registry.platformio.org`,
so none of the three fixes above have been verified by an actual successful
`pio run` — only by static/textual review and public version-compatibility
reports. Please run `pio run` yourself and report back if anything still
breaks; don't assume v2 is compiler-clean just because the obvious bugs are gone.
