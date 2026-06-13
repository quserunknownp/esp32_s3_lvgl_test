# Firmware Overview

이 디렉토리는 전기차(EV) 대시보드 시스템을 구동하는 핵심 소프트웨어 프로젝트들을 포함하고 있습니다. 역할에 따라 두 개의 별도 프로젝트로 분리되어 있습니다.

## 1. 🖥️ Dashboard Display (`dashboard_display/`)
*   **Platform**: ESP32-S3 (ESP-IDF v5.5.4)
*   **Role**: LVGL을 사용한 그래픽 사용자 인터페이스(UI) 처리 담당. CAN 버스로부터 데이터를 수신하여 800x480 RGB LCD에 60fps의 부드러운 애니메이션으로 렌더링합니다.
*   **Key Tech**: Octal PSRAM (PCLK 16MHz 최적화), TWAI (CAN) 인터럽트, LVGL Animations.
*   👉 [자세한 아키텍처 및 핀맵 보기](./dashboard_display/)

## 2. 🔌 RS485 to CAN Bridge (`rs485_can_bridge/`)
*   **Platform**: Arduino IDE 
*   **Role**: 중간 다리(Bridge) 역할. 배터리 관리 시스템(BMS)과 RS485 통신을 통해 데이터(SoC, 전압, 최고/최저 온도 등)를 읽어오고, 이를 파싱하여 대시보드가 읽을 수 있도록 CAN 버스에 뿌려줍니다.
*   **Key Tech**: Hardware Serial (RS485), TWAI (CAN 송신), FreeRTOS 백그라운드 태스크 분리.
*   👉 [소스코드 및 라이브러리 보기](./rs485_can_bridge/)
