#include "bms.h"
#include "driver/twai.h" 

// 주석 처리 해제 시 더미 데이터(RPM, 온도)를 전송하여 UI를 테스트할 수 있습니다.
//#define DUMMY_TEST 1 

// --- 핀 설정 ---
#define RS485_RX_PIN 16
#define RS485_TX_PIN 17
// RS485_CTRL_PIN(4)는 bms.cpp 내부에서 제어됨

#define CAN_RX_PIN 25
#define CAN_TX_PIN 26

OverkillSolarBms bms = OverkillSolarBms();

// 타이머 변수 2개 분리
uint32_t last_bms_update;
uint32_t last_toggle_update;

// 0과 1을 왔다 갔다 할 토글 변수
uint8_t toggle_state = 0; 

// --- FreeRTOS Task Handle ---
TaskHandle_t bmsTaskHandle;

// --- BMS RS485 수신 태스크 (Core 0 전담) ---
void bmsTask(void *pvParameters) {
    while (1) {
#ifndef DUMMY_TEST
        bms.main_task(); // 여기서 아무리 딜레이가 걸려도 Core 1(loop)에는 영향 없음!
#endif
        vTaskDelay(pdMS_TO_TICKS(10)); // WDT 방지 및 CPU 양보
    }
}

void setup() {
    delay(500);
    Serial.begin(115200);
    
    // 1. RS485 (BMS) 통신 초기화
    Serial2.begin(9600, SERIAL_8N1, RS485_RX_PIN, RS485_TX_PIN);
    while (!Serial2) { }
    bms.begin(&Serial2);

    // 2. CAN (TWAI) 통신 초기화 (속도는 로거에 맞게 수정 필요)
    twai_general_config_t g_config = TWAI_GENERAL_CONFIG_DEFAULT((gpio_num_t)CAN_TX_PIN, (gpio_num_t)CAN_RX_PIN, TWAI_MODE_NORMAL);
    twai_timing_config_t t_config = TWAI_TIMING_CONFIG_250KBITS(); 
    twai_filter_config_t f_config = TWAI_FILTER_CONFIG_ACCEPT_ALL();

    if (twai_driver_install(&g_config, &t_config, &f_config) == ESP_OK) {
        Serial.println("CAN 드라이버 설치 완료!");
    } else {
        Serial.println("CAN 드라이버 설치 실패...");
    }

    if (twai_start() == ESP_OK) {
        Serial.println("CAN 통신 시작!");
    }

    // 3. BMS 백그라운드 태스크 생성 (Core 0에 할당)
    xTaskCreatePinnedToCore(
        bmsTask,         /* Task 함수 */
        "BMS_Task",      /* Task 이름 */
        4096,            /* 스택 크기 */
        NULL,            /* 파라미터 */
        1,               /* 우선순위 */
        &bmsTaskHandle,  /* 핸들 */
        0                /* Core 0에 할당 */
    );

    // 타이머 초기화
    last_bms_update = millis();
    last_toggle_update = millis();
}

void loop() {
#ifndef DUMMY_TEST
    // bms.main_task()는 이제 Core 0에서 독립적으로 실행됩니다!
    // Core 1(루프)은 어떠한 블로킹도 받지 않습니다.

    // ==========================================
    // [작업 1] 1초(1000ms)마다 BMS 데이터 전송
    // ==========================================
    if (millis() - last_bms_update >= 1000) {
        //bms.debug();
        float voltage = bms.get_voltage();
        float current = bms.get_current();
        uint8_t soc = bms.get_state_of_charge();
        //Serial.println("%d", soc);

        int16_t v_int = (int16_t)(voltage * 100);
        int16_t c_int = (int16_t)(current * 100);

        twai_message_t msg_bms;
        msg_bms.identifier = 0x100; // BMS 데이터용 CAN ID
        msg_bms.extd = 0;
        msg_bms.data_length_code = 8;
        
        msg_bms.data[0] = v_int >> 8;
        msg_bms.data[1] = v_int & 0xFF;
        msg_bms.data[2] = c_int >> 8;
        msg_bms.data[3] = c_int & 0xFF;
        msg_bms.data[4] = soc;
        msg_bms.data[5] = 0;
        msg_bms.data[6] = 0;
        msg_bms.data[7] = 0;

        esp_err_t result = twai_transmit(&msg_bms, pdMS_TO_TICKS(10));
        if (result == ESP_OK) {
          Serial.printf("[CAN TX SUCCESS] SoC: %d%%, Volt: %.2fV, Curr: %.2fA\n", soc, voltage, current);
        } else {
        // 전송 실패 시 에러 코드와 함께 출력
          Serial.printf("[CAN TX FAILED] Error Code: %d (SoC: %d%%)\n", result, soc);
        }

        // --- Max NTC (최고 온도) 데이터 전송 ---
        uint8_t ntc_count = bms.get_num_ntcs();

        if (ntc_count > 0) {
            float max_temp = -999.0;
            for (uint8_t i = 0; i < ntc_count; i++) {
                float current_temp = bms.get_ntc_temperature(i);
                // Serial.printf("  - NTC[%d]: %.1f C\n", i, current_temp); // 모든 NTC 값 출력
                
                // inf 값이나 연결 안 된 센서(비현실적인 온도)를 걸러냅니다.
                if (current_temp > -50.0 && current_temp < 150.0) {
                    if (current_temp > max_temp) {
                        max_temp = current_temp;
                    }
                }
            }

            // 유효한 최고 온도를 찾았을 때만 전송
            if (max_temp > -999.0) {
                twai_message_t msg_max_ntc;
                msg_max_ntc.identifier = 0x101; // 대시보드 좌측 막대 (Max Battery Temp)
                msg_max_ntc.extd = 0;
                msg_max_ntc.data_length_code = 1;
                msg_max_ntc.data[0] = (uint8_t)max_temp;
                twai_transmit(&msg_max_ntc, pdMS_TO_TICKS(10));
                // Serial.printf("  -> Max Battery Temp 전송 (좌측): %.1f C\n", max_temp);
            }
        }
        // ------------------------------
        
        last_bms_update = millis();
    }

    // ==========================================
    // [작업 2] 0.5초(500ms)마다 토글 데이터 전송
    // ==========================================
    /*
    if (millis() - last_toggle_update >= 500) {
        twai_message_t msg_toggle;
        msg_toggle.identifier = 0x200; // 토글 데이터용 새로운 CAN ID
        msg_toggle.extd = 0;
        msg_toggle.data_length_code = 1; // 1바이트만 전송
        msg_toggle.data[0] = toggle_state; // 현재 상태(0 또는 1) 담기

        if (twai_transmit(&msg_toggle, pdMS_TO_TICKS(10)) == ESP_OK) {
            Serial.printf("토글 전송됨: ID 0x200, 값 %d\n", toggle_state);
        }

        // 상태 반전 (0이면 1로, 1이면 0으로)
        toggle_state = !toggle_state;
        
        last_toggle_update = millis();
    }
    */
#endif

    // ==========================================
    // [작업 3] 더미 데이터 전송 (UI 테스트용)
    // ==========================================
#ifdef DUMMY_TEST
    static uint32_t last_dummy_update = 0;
    uint32_t current_time = millis();
    if (current_time - last_dummy_update >= 50) { // 50ms (20Hz) 전송
        // millis()를 이용해 10초 주기로 0.0 ~ 1.0 ~ 0.0 삼각파 생성
        float phase = (float)(current_time % 10000) / 10000.0f;
        float triangle = (phase < 0.5f) ? (phase * 2.0f) : (2.0f - phase * 2.0f);

        // 범위에 맞춰 스케일링 (빨간색 범위 밖까지 넘어가도록 설정)
        int32_t dummy_rpm = (int32_t)(triangle * 8500.0f);               // 0 ~ 8500
        uint8_t dummy_bat_temp = (uint8_t)(20.0f + triangle * 50.0f);    // 20 ~ 70 (BAT Red 55)
        uint8_t dummy_inv_temp = (uint8_t)(20.0f + triangle * 70.0f);    // 20 ~ 90 (INV Red 70)
        int16_t dummy_motor_temp = (int16_t)(20.0f + triangle * 130.0f); // 20 ~ 150 (MOT Red 115)

        // 1. RPM 전송 (0x10A, Byte 4~7)
        twai_message_t msg_rpm = {0};
        msg_rpm.identifier = 0x10A;
        msg_rpm.extd = 0;
        msg_rpm.data_length_code = 8;
        msg_rpm.data[0] = 0; msg_rpm.data[1] = 0; msg_rpm.data[2] = 0; msg_rpm.data[3] = 0;
        msg_rpm.data[4] = dummy_rpm & 0xFF;
        msg_rpm.data[5] = (dummy_rpm >> 8) & 0xFF;
        msg_rpm.data[6] = (dummy_rpm >> 16) & 0xFF;
        msg_rpm.data[7] = (dummy_rpm >> 24) & 0xFF;
        twai_transmit(&msg_rpm, pdMS_TO_TICKS(10));

        // 2. BAT 온도 전송 (0x101) - 배터리 온도 추가!
        twai_message_t msg_bat = {0};
        msg_bat.identifier = 0x101;
        msg_bat.extd = 0;
        msg_bat.data_length_code = 1;
        msg_bat.data[0] = dummy_bat_temp;
        twai_transmit(&msg_bat, pdMS_TO_TICKS(10));

        // 3. 인버터 온도 전송 (0x102)
        twai_message_t msg_inv = {0};
        msg_inv.identifier = 0x102;
        msg_inv.extd = 0;
        msg_inv.data_length_code = 1;
        msg_inv.data[0] = dummy_inv_temp;
        twai_transmit(&msg_inv, pdMS_TO_TICKS(10));

        // 4. 모터 온도 전송 (0x10D)
        twai_message_t msg_mot = {0};
        msg_mot.identifier = 0x10D;
        msg_mot.extd = 0;
        msg_mot.data_length_code = 2;
        msg_mot.data[0] = dummy_motor_temp & 0xFF;
        msg_mot.data[1] = (dummy_motor_temp >> 8) & 0xFF;
        twai_transmit(&msg_mot, pdMS_TO_TICKS(10));

        last_dummy_update = current_time;
    }
#endif
}