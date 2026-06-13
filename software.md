# Software Documentation

## Overview
이 문서는 전기차(EV) 대시보드 프로젝트의 소프트웨어 아키텍처와 주요 구현 사항을 설명합니다. 메인 코드는 `firmware/` 디렉토리에 위치해 있습니다.

## 1. 개발 환경 (Development Environment)
*   **Framework**: ESP-IDF v5.5.4
*   **UI Library**: LVGL v8.4.0
*   **Build System**: CMake / Ninja

## 2. 주요 소프트웨어 아키텍처
*   **LVGL 렌더링 최적화**: 
    *   `LVGL_TASK_MAX_DELAY_MS`를 15ms로 설정하여 60fps 주사율을 강제 유지합니다. (500ms 지연 버그 해결)
    *   불연속적인 CAN 데이터(20Hz)를 부드럽게 표현하기 위해 `lv_anim_t` 기반의 값 보간(Interpolation) 애니메이션을 적극 활용합니다.
*   **메모리 관리 (PSRAM)**: 
    *   800x480 해상도의 프레임 버퍼(약 750KB)를 처리하기 위해 Octal PSRAM (80MHz)을 사용합니다.
    *   EDMA Starvation 현상을 방지하기 위해 PCLK를 16~18MHz 수준으로 제한하여 CPU 렌더링 대역폭(약 30MB/s) 마진을 확보했습니다.

## 3. 통신 프로토콜 (CAN / TWAI)
*   **Baudrate**: 250 Kbps
*   **수신 (RX)**: BMS 온도, 모터 RPM, 인버터 온도, SoC 데이터 파싱 (`0x10A`, `0x101`, `0x102` 등)
*   **송신 (TX)**: 10번 핀 물리 버튼 입력 시 Sevcon 드라이브 프로파일(토크맵) 변경 명령(`0x201`)을 CAN으로 송신합니다.
