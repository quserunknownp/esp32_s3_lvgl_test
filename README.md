# ESP32-S3 LVGL Dashboard

A high-performance vehicle dashboard UI built with LVGL and ESP-IDF, specifically configured for ESP32-S3 N16R8 modules and an 800x480 RGB LCD.

## Hardware Specifications

*   **MCU**: ESP32-S3 (N16R8)
    *   **Flash**: 16 MB (QIO)
    *   **PSRAM**: 8 MB (Octal SPI)
*   **Display**: 5.0" RGB LCD (QT-5000H40R79L-6N5W12 / ILI6485)
    *   **Resolution**: 800 x 480
    *   **Interface**: 16-bit RGB (RGB565)
    *   **Pixel Clock**: 25 MHz

## Pin Configuration (RGB Interface)

The display uses the following GPIO mapping for the RGB interface:

*   **Control Pins**:
    *   `PCLK`: 11
    *   `HSYNC`: 12
    *   `VSYNC`: 13
    *   `DE`: 14
*   **Red (R3~R7)**: 4, 5, 6, 7, 15
*   **Green (G2~G7)**: 16, 17, 18, 8, 3, 9
*   **Blue (B3~B7)**: 42, 41, 40, 39, 38

## Software Stack

*   **Framework**: [ESP-IDF v5.5.4](https://github.com/espressif/esp-idf)
*   **UI Library**: [LVGL v8.4.0](https://lvgl.io/)
*   **Optimization**: `-Os` (Size optimization is used to bypass a GCC 14.2 bug for RGB panels).

## UI Features

> **Note**: The current UI features and layout are for demonstration and testing purposes, and are subject to change.

The dashboard provides a sleek, dark-themed UI featuring:
*   **RPM Arc**: A large, central 12000 RPM gauge (550x550) with animated revving.
*   **Temperature Bar**: Engine coolant temperature candle bar on the left side.
*   **State of Charge (SoC)**: Battery percentage candle bar on the right side.
*   **Inverter Temperature**: Wide temperature bar at the bottom center.

## Build and Flash

1.  Make sure you have ESP-IDF v5.5.4 installed and activated.
2.  Set the target to ESP32-S3:
    ```bash
    idf.py set-target esp32s3
    ```
3.  Build, flash, and monitor:
    ```bash
    idf.py build flash monitor
    ```
