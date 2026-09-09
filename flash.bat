@echo off
REM ============================================================================
REM flash.bat — Flash CYD Wi-Fi Radar firmware via esptool.py
REM
REM Prerequisite: run "pio run" in this project directory first. This script
REM does NOT build the firmware — it flashes whatever .pio\build\esp32dev\
REM already contains.
REM
REM Usage: flash.bat [COM_PORT]
REM   e.g. flash.bat COM5
REM If no port is given, esptool will attempt auto-detection.
REM ============================================================================
setlocal

set BUILD_DIR=.pio\build\esp32dev
set BOOTLOADER=%BUILD_DIR%\bootloader.bin
set PARTITIONS=%BUILD_DIR%\partitions.bin
set FIRMWARE=%BUILD_DIR%\firmware.bin
set BAUD=921600
set PORT=%1

if not exist "%BOOTLOADER%" (
    echo ERROR: %BOOTLOADER% not found.
    echo Run "pio run" in this directory first to build the firmware.
    exit /b 1
)
if not exist "%PARTITIONS%" (
    echo ERROR: %PARTITIONS% not found.
    echo Run "pio run" in this directory first to build the firmware.
    exit /b 1
)
if not exist "%FIRMWARE%" (
    echo ERROR: %FIRMWARE% not found.
    echo Run "pio run" in this directory first to build the firmware.
    exit /b 1
)

set PORT_ARG=
if not "%PORT%"=="" set PORT_ARG=--port %PORT%

esptool.py %PORT_ARG% ^
    --chip esp32 ^
    --baud %BAUD% ^
    --before default_reset ^
    --after hard_reset ^
    write_flash -z ^
    --flash_mode dio ^
    --flash_freq 40m ^
    --flash_size detect ^
    0x1000  "%BOOTLOADER%" ^
    0x8000  "%PARTITIONS%" ^
    0x10000 "%FIRMWARE%"

echo Flash complete.
endlocal
