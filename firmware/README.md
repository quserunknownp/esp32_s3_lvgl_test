# Firmware Overview

This directory contains the core software projects that power the EV Dashboard System. It is divided into two separate projects based on their hardware platforms and roles.

## 1. 🖥️ [Dashboard Display](./dashboard_display/README.md)
*   **Platform**: ESP32-S3 (Built with ESP-IDF v5.5.4)
*   **Role**: Handles the Graphical User Interface (GUI) using LVGL. Receives telemetry data from the CAN bus and renders stable ~35fps animations on an 800x480 RGB LCD.
*   **Key Technologies**: Octal PSRAM (Optimized 16MHz PCLK), TWAI (CAN) interrupts, LVGL Animations.

## 2. 🔌 [RS485 to CAN Bridge](./rs485_can_bridge/)
*   **Platform**: Arduino IDE (ESP32/AVR)
*   **Role**: Acts as a communication bridge. It reads Battery Management System (BMS) data via RS485 (Modbus/Custom Protocol), parses it (SoC, Voltages, Max/Min Temperatures), and broadcasts it onto the CAN bus for the dashboard to consume.
*   **Key Technologies**: Hardware Serial (RS485), TWAI (CAN TX), FreeRTOS background tasks.

Use the links above to navigate to the source code and specific build instructions for each component.

[⬅️ Back to Main Repository](../README.md)
