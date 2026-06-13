# EV Dashboard System

Welcome to the EV Dashboard System monorepo. This repository contains the complete hardware and software stack for a high-performance vehicle dashboard UI, driven by an ESP32-S3 and an Arduino-based CAN bridge.

## Repository Structure

*   📁 **[`firmware/dashboard_display`](./firmware/dashboard_display/)**: The main ESP32-S3 firmware built with ESP-IDF and LVGL. Renders a smooth 60fps UI on an 800x480 RGB LCD.
*   📁 **[`firmware/rs485_can_bridge`](./firmware/rs485_can_bridge/)**: Arduino code for the intermediate node that reads BMS data via RS485 and forwards it to the CAN bus.
*   📁 **[`hardware/housing_step`](./hardware/housing_step/)**: 3D CAD files (.step) and 2D dimensions for the dashboard's physical enclosure and brackets.
*   📁 **[`hardware/pcb`](./hardware/pcb/)**: KiCad PCB design files for the custom dashboard carrier board, including manufacturing gerbers.

Please navigate to each directory to read its specific `README.md` for build instructions, pinouts, and hardware notes.
