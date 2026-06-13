# Hardware Documentation

## Overview
이 문서는 대시보드 시스템을 구성하는 하드웨어 부품, 핀맵 및 회로 구성에 대한 정보를 담고 있습니다. 추후 회로도(Schematics)나 3D 모델링 파일이 추가될 수 있습니다.

## 1. 주요 부품 (Core Components)
*   **Microcontroller (MCU)**: [ESP32-S3-DevKitC-1](https://docs.espressif.com/projects/esp-dev-kits/en/latest/esp32s3/esp32-s3-devkitc-1) (N16R8)
    *   16MB Flash, 8MB Octal PSRAM 탑재 모델 필수
    *   > [!WARNING]
    *   > **폼팩터 주의사항**: USB Type-C 포트가 장착된 보드의 경우, 기존 보드보다 좌우 폭이 **2.54mm (0.1인치, 브레드보드 1칸)** 더 넓을 수 있습니다. 향후 커스텀 PCB 설계나 3D 하우징 제작 시 핀 간격(Pitch)과 전체 폭을 반드시 실측하여 반영하시기 바랍니다.
*   **Display**: 5.0인치 RGB LCD (해상도: 800x480, 드라이버 IC: ILI6485 등)
*   **CAN Transceiver**: SN65HVD230 (3.3V) 또는 TJA1050 (5V 로직 레벨 시프터 필요)
*   **BMS Bridge**: Arduino 기반 RS485 to CAN 변환 노드

## 2. 핀맵 (Pin Mappings)

### 디스플레이 (RGB Interface)
*   **Control**: `PCLK` (11), `HSYNC` (12), `VSYNC` (13), `DE` (14)
*   **Red**: `R3`~`R7` (4, 5, 6, 7, 15)
*   **Green**: `G2`~`G7` (16, 17, 18, 8, 3, 9)
*   **Blue**: `B3`~`B7` (42, 41, 40, 39, 38)

### 통신 및 제어 (Peripherals)
*   **CAN RX**: GPIO 2
*   **CAN TX**: GPIO 1
*   **Drive Mode Switch**: GPIO 10 (내부 풀업 저항 사용, GND와 쇼트 시 Active Low 동작)
