#!/usr/bin/env bash
# ============================================================================
# flash.sh — Flash CYD Wi-Fi Radar firmware via esptool.py
#
# Prerequisite: run `pio run` in this project directory first. This script
# does NOT build the firmware — it flashes whatever .pio/build/esp32dev/
# already contains, so it will fail loudly (not silently) if you haven't
# built yet.
#
# Usage: ./flash.sh [serial_port]
#   e.g. ./flash.sh /dev/ttyUSB0
# If no port is given, esptool will attempt auto-detection.
# ============================================================================
set -euo pipefail

BUILD_DIR=".pio/build/esp32dev"
BOOTLOADER="${BUILD_DIR}/bootloader.bin"
PARTITIONS="${BUILD_DIR}/partitions.bin"
FIRMWARE="${BUILD_DIR}/firmware.bin"
BAUD=921600
PORT="${1:-}"

for f in "$BOOTLOADER" "$PARTITIONS" "$FIRMWARE"; do
    if [ ! -f "$f" ]; then
        echo "ERROR: $f not found."
        echo "Run 'pio run' in this directory first to build the firmware."
        exit 1
    fi
done

PORT_ARG=()
if [ -n "$PORT" ]; then
    PORT_ARG=(--port "$PORT")
fi

esptool.py "${PORT_ARG[@]}" \
    --chip esp32 \
    --baud "$BAUD" \
    --before default_reset \
    --after hard_reset \
    write_flash -z \
    --flash_mode dio \
    --flash_freq 40m \
    --flash_size detect \
    0x1000  "$BOOTLOADER" \
    0x8000  "$PARTITIONS" \
    0x10000 "$FIRMWARE"

echo "Flash complete."
