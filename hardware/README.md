# Hardware Documentation

## Overview
This document provides information about the hardware components, pin mappings, and circuit configurations that make up the EV Dashboard System.

## 1. Core Components
*   **Microcontroller (MCU)**: [ESP32-S3-DevKitC-1](https://docs.espressif.com/projects/esp-dev-kits/en/latest/esp32s3/esp32-s3-devkitc-1) (N16R8)
    *   Must be the model with 16MB Flash and 8MB Octal PSRAM.
    *   > [!WARNING]
    *   > **Form Factor Note**: Be aware that certain revisions or clones of this board equipped with a **USB Type-C port** can be **2.54mm (0.1 inch, exactly one breadboard row) wider** than the standard Micro-USB versions. Please manually measure the pin pitch and total width when designing custom PCBs or 3D printed housings.
*   **Display**: 5.0-inch RGB LCD (Resolution: 800x480, Driver IC: ILI6485)
*   **CAN Transceiver**: SN65HVD230 (3.3V) or TJA1050 (requires a 5V logic level shifter).
*   **BMS Bridge**: Arduino-based RS485 to CAN translation node.

## 2. Directory Structure

*   📁 **[`housing_step/`](./housing_step/)**: Contains 3D CAD models (.step) and 2D dimensional drawings (.pdf) for the dashboard's physical enclosure and mounting brackets.
*   📁 **[`pcb/`](./pcb/)**: Contains the KiCad project files and manufacturing production data (Gerbers, BOM, Pick and Place) for the custom dashboard carrier board.

## 3. Pin Mappings

### RGB Display Interface
*   **Control**: `PCLK` (11), `HSYNC` (12), `VSYNC` (13), `DE` (14)
*   **Red**: `R3`~`R7` (4, 5, 6, 7, 15)
*   **Green**: `G2`~`G7` (16, 17, 18, 8, 3, 9)
*   **Blue**: `B3`~`B7` (42, 41, 40, 39, 38)

### Peripherals
*   **CAN RX**: GPIO 2
*   **CAN TX**: GPIO 1
*   **Drive Mode Switch**: GPIO 10 (Uses internal pull-up, triggers Active Low when shorted to GND).

[⬅️ Back to Main Repository](../README.md)
