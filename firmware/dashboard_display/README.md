# ESP32-S3 LVGL EV Dashboard

A high-performance vehicle dashboard UI built with LVGL and ESP-IDF, specifically configured for ESP32-S3 N16R8 modules and an 800x480 RGB LCD. It receives telemetry data over CAN Bus (TWAI) and renders stable ~35fps animations.

## Hardware Specifications

*   **MCU**: ESP32-S3 (N16R8)
    *   **Flash**: 16 MB (QIO)
    *   **PSRAM**: 8 MB (Octal SPI @ 80MHz) - *Required for 800x480 Framebuffers*
*   **Display**: 5.0" RGB LCD (QT-5000H40R79L-6N5W12 / ILI6485)
    *   **Resolution**: 800 x 480
    *   **Interface**: 16-bit RGB (RGB565)
    *   **Optimal Pixel Clock**: `16 MHz` ~ `18 MHz` (Lowered from 25MHz to prevent EDMA starvation and PSRAM bandwidth bottlenecks during LVGL cache misses). At this clock, the physical refresh rate is capped at around 35 FPS.
*   **CAN Transceiver**: TJA1050 / SN65HVD230 (Connected to GPIO 1 & 2)

## Pin Configuration

### RGB LCD Interface
*   **Control Pins**: `PCLK`: 11, `HSYNC`: 12, `VSYNC`: 13, `DE`: 14
*   **Red (R3~R7)**: 4, 5, 6, 7, 15
*   **Green (G2~G7)**: 16, 17, 18, 8, 3, 9
*   **Blue (B3~B7)**: 42, 41, 40, 39, 38

### Peripherals
*   **CAN TX**: GPIO 1
*   **CAN RX**: GPIO 2
*   **Mode Switch Button**: GPIO 10 (Internal Pull-up, active low)

## Software Architecture & Features

*   **LVGL Rendering**: LVGL task delay is configured to `LVGL_TASK_MAX_DELAY_MS 15` to ensure immediate UI updates, yielding a stable ~35 FPS bounded by the 16MHz PCLK limit. UI components use `lv_anim_t` to interpolate discrete CAN messages (e.g., 20Hz) into perfectly smooth visuals.
*   **CAN Communication (TWAI)**: Operates at 250Kbps. Listens for 0x10A (RPM), 0x101 (Battery Temp), 0x102 (Inverter Temp), 0x103 (Motor Temp), and 0x104 (SoC).
*   **Drive Mode Toggle**: Shorting GPIO 10 to GND toggles the torque mode.
    *   UI: Top-left LED turns Green (ECO) or Red (SPORT).
    *   CAN: Sends a 1-byte message to `0x201` (`0x00` or `0x01`) to switch the Sevcon Motor Controller's Drive Profile.
*   **Dynamic Temperature Candles**: Thermal bars implement 3-stage color gradients (Green -> Yellow -> Red) based on safe operating limits.

## Build and Flash

1.  Make sure you have [ESP-IDF v5.5.4](https://github.com/espressif/esp-idf) installed.
2.  Navigate to this directory: `cd firmware/dashboard_display`
3.  Set the target: `idf.py set-target esp32s3`
4.  Build, flash, and monitor: `idf.py build flash monitor`

[⬅️ Back to Firmware Overview](../README.md) | [⬅️ Back to Main Repository](../../README.md)
