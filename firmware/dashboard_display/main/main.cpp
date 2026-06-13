#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_lcd_panel_rgb.h"
#include "esp_lcd_panel_ops.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "lvgl.h"
#include "esp_cache.h"
#include "driver/twai.h"
#include "driver/gpio.h"
#include "freertos/semphr.h"

static const char *TAG = "LVGL_RGB_Test";

// --- LCD Specification ---
#define EXAMPLE_LCD_H_RES              800
#define EXAMPLE_LCD_V_RES              480
#define EXAMPLE_LCD_PIXEL_CLOCK_HZ     (16 * 1000 * 1000) // 16 MHz (안정적인 16-bit 대역폭)

// ESP32-S3-N16R8 Development Board RGB GPIO configuration
// Pinmap configuration for EV Dashboard

#define EXAMPLE_PIN_NUM_HSYNC          (12)
#define EXAMPLE_PIN_NUM_VSYNC          (13)
#define EXAMPLE_PIN_NUM_DE             (14)
#define EXAMPLE_PIN_NUM_PCLK           (11)
#define EXAMPLE_PIN_NUM_DISP_EN        (-1)

// R (5 bits) - LCD Panel R3~R7
#define EXAMPLE_PIN_NUM_R3             (4)
#define EXAMPLE_PIN_NUM_R4             (5)
#define EXAMPLE_PIN_NUM_R5             (6)
#define EXAMPLE_PIN_NUM_R6             (7)
#define EXAMPLE_PIN_NUM_R7             (15)

// G (6 bits) - LCD Panel G2~G7
#define EXAMPLE_PIN_NUM_G2             (16)
#define EXAMPLE_PIN_NUM_G3             (17)
#define EXAMPLE_PIN_NUM_G4             (18)
#define EXAMPLE_PIN_NUM_G5             (8)
#define EXAMPLE_PIN_NUM_G6             (3)
#define EXAMPLE_PIN_NUM_G7             (9)

// B (5 bits) - LCD Panel B3~B7
#define EXAMPLE_PIN_NUM_B3             (42)
#define EXAMPLE_PIN_NUM_B4             (41)
#define EXAMPLE_PIN_NUM_B5             (40)
#define EXAMPLE_PIN_NUM_B6             (39)
#define EXAMPLE_PIN_NUM_B7             (38)

// LVGL Task parameters
#define LVGL_TASK_MAX_DELAY_MS 15
#define LVGL_TASK_MIN_DELAY_MS 1
#define LVGL_TASK_STACK_SIZE   (4 * 1024)
#define LVGL_TASK_PRIORITY     2

// CAN (TWAI) parameters
#define CAN_TX_PIN 1
#define CAN_RX_PIN 2

// Thread safety for LVGL
static SemaphoreHandle_t lvgl_mux = NULL;

static esp_lcd_panel_handle_t panel_handle = NULL;
static lv_disp_draw_buf_t disp_buf;

// RGB panel typically needs double buffering in PSRAM or SRAM
static lv_color_t *buf1 = NULL;
static lv_color_t *buf2 = NULL;

static void example_lvgl_flush_cb(lv_disp_drv_t *drv, const lv_area_t *area, lv_color_t *color_map)
{
    esp_lcd_panel_handle_t panel = (esp_lcd_panel_handle_t)drv->user_data;
    
    int offsetx1 = area->x1;
    int offsetx2 = area->x2;
    int offsety1 = area->y1;
    int offsety2 = area->y2;
    
    // Internal SRAM에 그려진 조각을 PSRAM 프레임 버퍼로 고속 복사
    esp_lcd_panel_draw_bitmap(panel, offsetx1, offsety1, offsetx2 + 1, offsety2 + 1, color_map);
    lv_disp_flush_ready(drv);
}

static void example_increase_lvgl_tick(void *arg)
{
    lv_tick_inc(1);
}

static void lvgl_port_task(void *arg)
{
    ESP_LOGI(TAG, "Starting LVGL task");
    uint32_t task_delay_ms = LVGL_TASK_MAX_DELAY_MS;
    while (1) {
        if (xSemaphoreTake(lvgl_mux, portMAX_DELAY) == pdTRUE) {
            task_delay_ms = lv_timer_handler();
            xSemaphoreGive(lvgl_mux);
        }
        
        if (task_delay_ms > LVGL_TASK_MAX_DELAY_MS) {
            task_delay_ms = LVGL_TASK_MAX_DELAY_MS;
        } else if (task_delay_ms < LVGL_TASK_MIN_DELAY_MS) {
            task_delay_ms = LVGL_TASK_MIN_DELAY_MS;
        }
        vTaskDelay(pdMS_TO_TICKS(task_delay_ms));
    }
}

// --- Mode Button ---
#define MODE_BUTTON_PIN 10 // 모드 전환 스위치 연결 핀 (물리 버튼)
static uint8_t drive_mode = 0; // 0: ECO, 1: SPORT

// --- LVGL Dashboard UI (WoB) ---
static lv_obj_t * rpm_label_ptr = NULL;
static lv_obj_t * bat_temp_label_ptr = NULL;
static lv_obj_t * inv_temp_label_ptr = NULL;
static lv_obj_t * motor_temp_label_ptr = NULL;
static lv_obj_t * soc_label_ptr = NULL;
static lv_obj_t * mode_led_ptr = NULL;

static lv_obj_t * global_rpm_arc = NULL;
static lv_obj_t * global_bat_bar = NULL;
static lv_obj_t * global_inv_bar = NULL;
static lv_obj_t * global_motor_bar = NULL;
static lv_obj_t * global_soc_bar = NULL;

static void set_arc_anim_cb(void * var, int32_t v) {
    lv_arc_set_value((lv_obj_t *)var, v);
}

static void set_rpm_value(void * obj, int32_t v)
{
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, obj);
    lv_anim_set_exec_cb(&a, set_arc_anim_cb);
    lv_anim_set_time(&a, 100); // 100ms 보간 애니메이션
    lv_anim_set_values(&a, lv_arc_get_value((lv_obj_t *)obj), v);
    lv_anim_start(&a);

    if(rpm_label_ptr) {
        lv_label_set_text_fmt(rpm_label_ptr, "%d", (int)v);
    }
}

static void set_bat_temp_value(void * obj, int32_t v)
{
    lv_bar_set_value((lv_obj_t *)obj, v, LV_ANIM_ON);
    if(bat_temp_label_ptr) {
        lv_label_set_text_fmt(bat_temp_label_ptr, "BAT\n%d C", (int)v);
    }
    if (v >= 55) {
        lv_obj_set_style_bg_color((lv_obj_t *)obj, lv_color_hex(0xFF0000), LV_PART_INDICATOR);
    } else if (v >= 50) {
        lv_obj_set_style_bg_color((lv_obj_t *)obj, lv_color_hex(0xFFFF00), LV_PART_INDICATOR);
    } else {
        lv_obj_set_style_bg_color((lv_obj_t *)obj, lv_color_hex(0x00FF00), LV_PART_INDICATOR);
    }
}

static void set_inv_temp_value(void * obj, int32_t v)
{
    lv_bar_set_value((lv_obj_t *)obj, v, LV_ANIM_ON);
    if(inv_temp_label_ptr) {
        lv_label_set_text_fmt(inv_temp_label_ptr, "INV\n%d C", (int)v);
    }
    if (v >= 70) {
        lv_obj_set_style_bg_color((lv_obj_t *)obj, lv_color_hex(0xFF0000), LV_PART_INDICATOR);
    } else if (v >= 60) {
        lv_obj_set_style_bg_color((lv_obj_t *)obj, lv_color_hex(0xFFFF00), LV_PART_INDICATOR);
    } else {
        lv_obj_set_style_bg_color((lv_obj_t *)obj, lv_color_hex(0x00FF00), LV_PART_INDICATOR);
    }
}

static void set_motor_temp_value(void * obj, int32_t v)
{
    lv_bar_set_value((lv_obj_t *)obj, v, LV_ANIM_ON);
    if(motor_temp_label_ptr) {
        lv_label_set_text_fmt(motor_temp_label_ptr, "MOT\n%d C", (int)v);
    }
    if (v >= 115) {
        lv_obj_set_style_bg_color((lv_obj_t *)obj, lv_color_hex(0xFF0000), LV_PART_INDICATOR);
    } else if (v >= 100) {
        lv_obj_set_style_bg_color((lv_obj_t *)obj, lv_color_hex(0xFFFF00), LV_PART_INDICATOR);
    } else {
        lv_obj_set_style_bg_color((lv_obj_t *)obj, lv_color_hex(0x00FF00), LV_PART_INDICATOR);
    }
}

static void set_soc_value(void * obj, int32_t v)
{
    lv_bar_set_value((lv_obj_t *)obj, v, LV_ANIM_OFF);
    if(soc_label_ptr) {
        lv_label_set_text_fmt(soc_label_ptr, "SoC: %d %%", (int)v);
    }
}

// --- TWAI Receive Task ---
static void twai_receive_task(void *arg)
{
    twai_message_t message;
    ESP_LOGI(TAG, "Starting TWAI receive task");
    
    while (1) {
        if (twai_receive(&message, pdMS_TO_TICKS(1000)) == ESP_OK) {
            // We successfully received a message!
            if (xSemaphoreTake(lvgl_mux, portMAX_DELAY) == pdTRUE) {
                // Determine what to update based on CAN ID
                // Example mock IDs:
                // 0x100: RPM (2 bytes)
                // 0x101: Motor Temp (1 byte)
                // 0x102: Inv Temp (1 byte)
                // 0x103: SoC (1 byte)
                
                if (message.identifier == 0x100 && message.data_length_code >= 5) {
                    // data[4]: SoC
                    uint8_t soc = message.data[4];
                    if (global_soc_bar) set_soc_value(global_soc_bar, soc);
                } else if (message.identifier == 0x10A && message.data_length_code >= 8) {
                    // Sevcon TPDO 1 (0x10A)
                    // Byte 4,5,6,7: Velocity (32-bit)
                    int32_t velocity = message.data[4] | (message.data[5] << 8) | (message.data[6] << 16) | (message.data[7] << 24);
                    if (global_rpm_arc) set_rpm_value(global_rpm_arc, velocity);
                } else if (message.identifier == 0x101 && message.data_length_code >= 1) {
                    if (global_bat_bar) set_bat_temp_value(global_bat_bar, message.data[0]);
                } else if (message.identifier == 0x102 && message.data_length_code >= 1) {
                    if (global_inv_bar) set_inv_temp_value(global_inv_bar, message.data[0]);
                } else if (message.identifier == 0x10D && message.data_length_code >= 2) {
                    // Motor Temp from Sevcon TPDO 4
                    int16_t motor_temp = message.data[0] | (message.data[1] << 8);
                    if (global_motor_bar) set_motor_temp_value(global_motor_bar, motor_temp);
                } else if (message.identifier == 0x103 && message.data_length_code >= 1) {
                    if (global_soc_bar) set_soc_value(global_soc_bar, message.data[0]);
                }
                
                xSemaphoreGive(lvgl_mux);
            }
        }
    }
}

// --- Button Task (Mode Switch) ---
static void button_task(void *arg)
{
    gpio_config_t btn_cfg = {};
    btn_cfg.pin_bit_mask = (1ULL << MODE_BUTTON_PIN);
    btn_cfg.mode = GPIO_MODE_INPUT;
    btn_cfg.pull_up_en = GPIO_PULLUP_ENABLE;
    btn_cfg.pull_down_en = GPIO_PULLDOWN_DISABLE;
    btn_cfg.intr_type = GPIO_INTR_DISABLE;
    gpio_config(&btn_cfg);

    uint8_t last_state = 1;
    ESP_LOGI(TAG, "Starting Button task on GPIO %d", MODE_BUTTON_PIN);

    while (1) {
        uint8_t current_state = gpio_get_level((gpio_num_t)MODE_BUTTON_PIN);
        if (last_state == 1 && current_state == 0) {
            // Button pressed (active low)
            vTaskDelay(pdMS_TO_TICKS(50)); // Debounce
            if (gpio_get_level((gpio_num_t)MODE_BUTTON_PIN) == 0) {
                drive_mode = !drive_mode; // Toggle
                
                // Update UI
                if (xSemaphoreTake(lvgl_mux, portMAX_DELAY) == pdTRUE) {
                    if (mode_led_ptr) {
                        if (drive_mode) {
                            lv_led_set_color(mode_led_ptr, lv_color_hex(0xFF0000)); // 고토크: 빨간색
                        } else {
                            lv_led_set_color(mode_led_ptr, lv_color_hex(0x00FF00)); // 저토크: 초록색
                        }
                        lv_led_on(mode_led_ptr);
                    }
                    xSemaphoreGive(lvgl_mux);
                }

                // Send CAN Message to Sevcon (RPDO)
                twai_message_t msg;
                msg.identifier = 0x201; // Example: RPDO 1 for Node 1
                msg.extd = 0;
                msg.data_length_code = 1;
                msg.data[0] = drive_mode; // 0 or 1
                
                if (twai_transmit(&msg, pdMS_TO_TICKS(10)) == ESP_OK) {
                    ESP_LOGI(TAG, "Mode switched to %d, CAN message sent", drive_mode);
                } else {
                    ESP_LOGE(TAG, "Failed to send Mode CAN message");
                }
                
                // Wait for release
                while(gpio_get_level((gpio_num_t)MODE_BUTTON_PIN) == 0) {
                    vTaskDelay(pdMS_TO_TICKS(10));
                }
            }
        }
        last_state = current_state;
        vTaskDelay(pdMS_TO_TICKS(20)); // Poll every 20ms
    }
}

void create_dashboard_ui(void)
{
    // 1. Theme: Black Background
    lv_obj_set_style_bg_color(lv_scr_act(), lv_color_black(), 0);

    // 2. RPM Arc (Left side)
    global_rpm_arc = lv_arc_create(lv_scr_act());
    lv_arc_set_rotation(global_rpm_arc, 135);
    lv_arc_set_bg_angles(global_rpm_arc, 0, 270);
    lv_arc_set_range(global_rpm_arc, 0, 8000);
    lv_obj_set_size(global_rpm_arc, 340, 340);
    lv_obj_align(global_rpm_arc, LV_ALIGN_LEFT_MID, 30, -30);
    lv_obj_remove_style(global_rpm_arc, NULL, LV_PART_KNOB);
    lv_obj_clear_flag(global_rpm_arc, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_arc_color(global_rpm_arc, lv_color_hex(0x222222), LV_PART_MAIN);
    lv_obj_set_style_arc_color(global_rpm_arc, lv_color_hex(0x00FFFF), LV_PART_INDICATOR);
    lv_obj_set_style_arc_width(global_rpm_arc, 30, LV_PART_MAIN);
    lv_obj_set_style_arc_width(global_rpm_arc, 30, LV_PART_INDICATOR);

    rpm_label_ptr = lv_label_create(lv_scr_act());
    lv_label_set_text(rpm_label_ptr, "0");
    lv_obj_set_style_text_color(rpm_label_ptr, lv_color_white(), 0);
    lv_obj_set_style_text_font(rpm_label_ptr, &lv_font_montserrat_48, 0);
    lv_obj_align_to(rpm_label_ptr, global_rpm_arc, LV_ALIGN_CENTER, 0, -10);

    lv_obj_t * rpm_sub = lv_label_create(lv_scr_act());
    lv_label_set_text(rpm_sub, "RPM");
    lv_obj_set_style_text_color(rpm_sub, lv_color_hex(0x888888), 0);
    lv_obj_set_style_text_font(rpm_sub, &lv_font_montserrat_24, 0);
    lv_obj_align_to(rpm_sub, rpm_label_ptr, LV_ALIGN_OUT_BOTTOM_MID, 0, 10);

    // 3. 3 Vertical Candles (Right side: BAT, INV, MOTOR)
    // BAT (Leftmost of the 3)
    global_bat_bar = lv_bar_create(lv_scr_act());
    lv_obj_set_size(global_bat_bar, 40, 240);
    lv_obj_align(global_bat_bar, LV_ALIGN_RIGHT_MID, -210, -30);
    lv_bar_set_range(global_bat_bar, 20, 60);
    lv_obj_set_style_bg_color(global_bat_bar, lv_color_hex(0x222222), LV_PART_MAIN);
    lv_obj_set_style_bg_color(global_bat_bar, lv_color_hex(0x00FF00), LV_PART_INDICATOR);
    bat_temp_label_ptr = lv_label_create(lv_scr_act());
    lv_label_set_text(bat_temp_label_ptr, "BAT\n0 C");
    lv_obj_set_style_text_color(bat_temp_label_ptr, lv_color_white(), 0);
    lv_obj_set_style_text_align(bat_temp_label_ptr, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align_to(bat_temp_label_ptr, global_bat_bar, LV_ALIGN_OUT_BOTTOM_MID, 0, 10);

    // INV (Middle of the 3)
    global_inv_bar = lv_bar_create(lv_scr_act());
    lv_obj_set_size(global_inv_bar, 40, 240);
    lv_obj_align(global_inv_bar, LV_ALIGN_RIGHT_MID, -120, -30);
    lv_bar_set_range(global_inv_bar, 20, 85);
    lv_obj_set_style_bg_color(global_inv_bar, lv_color_hex(0x222222), LV_PART_MAIN);
    lv_obj_set_style_bg_color(global_inv_bar, lv_color_hex(0x00FF00), LV_PART_INDICATOR);
    inv_temp_label_ptr = lv_label_create(lv_scr_act());
    lv_label_set_text(inv_temp_label_ptr, "INV\n0 C");
    lv_obj_set_style_text_color(inv_temp_label_ptr, lv_color_white(), 0);
    lv_obj_set_style_text_align(inv_temp_label_ptr, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align_to(inv_temp_label_ptr, global_inv_bar, LV_ALIGN_OUT_BOTTOM_MID, 0, 10);

    // MOTOR (Rightmost of the 3)
    global_motor_bar = lv_bar_create(lv_scr_act());
    lv_obj_set_size(global_motor_bar, 40, 240);
    lv_obj_align(global_motor_bar, LV_ALIGN_RIGHT_MID, -30, -30);
    lv_bar_set_range(global_motor_bar, 20, 140);
    lv_obj_set_style_bg_color(global_motor_bar, lv_color_hex(0x222222), LV_PART_MAIN);
    lv_obj_set_style_bg_color(global_motor_bar, lv_color_hex(0x00FF00), LV_PART_INDICATOR); // Green for Motor
    motor_temp_label_ptr = lv_label_create(lv_scr_act());
    lv_label_set_text(motor_temp_label_ptr, "MOT\n0 C");
    lv_obj_set_style_text_color(motor_temp_label_ptr, lv_color_white(), 0);
    lv_obj_set_style_text_align(motor_temp_label_ptr, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align_to(motor_temp_label_ptr, global_motor_bar, LV_ALIGN_OUT_BOTTOM_MID, 0, 10);

    // 4. SoC Bar (Very Bottom Center)
    global_soc_bar = lv_bar_create(lv_scr_act());
    lv_obj_set_size(global_soc_bar, 600, 30);
    lv_obj_align(global_soc_bar, LV_ALIGN_BOTTOM_MID, 0, -50);
    lv_bar_set_range(global_soc_bar, 0, 100);
    lv_obj_set_style_bg_color(global_soc_bar, lv_color_hex(0x222222), LV_PART_MAIN);
    lv_obj_set_style_bg_color(global_soc_bar, lv_color_hex(0x44FF44), LV_PART_INDICATOR);

    soc_label_ptr = lv_label_create(lv_scr_act());
    lv_label_set_text(soc_label_ptr, "SoC: 0 %");
    lv_obj_set_style_text_color(soc_label_ptr, lv_color_white(), 0);
    lv_obj_set_style_text_font(soc_label_ptr, &lv_font_montserrat_24, 0);
    lv_obj_align_to(soc_label_ptr, global_soc_bar, LV_ALIGN_OUT_BOTTOM_MID, 0, 10);
    
    // 7. Drive Mode Label (Top Left)
    mode_led_ptr = lv_led_create(lv_scr_act());
    lv_obj_set_size(mode_led_ptr, 40, 40); // 40x40 둥근 원
    lv_obj_align(mode_led_ptr, LV_ALIGN_TOP_LEFT, 20, 20);
    lv_led_set_color(mode_led_ptr, lv_color_hex(0x00FF00)); // 초기값: 초록색
    lv_led_on(mode_led_ptr);

    // (Animations removed to allow pure CAN control)
}

extern "C" void app_main(void)
{
    lvgl_mux = xSemaphoreCreateMutex();
    
    // Initialize TWAI (CAN) Driver
    twai_general_config_t g_config = TWAI_GENERAL_CONFIG_DEFAULT((gpio_num_t)CAN_TX_PIN, (gpio_num_t)CAN_RX_PIN, TWAI_MODE_NORMAL);
    twai_timing_config_t t_config = TWAI_TIMING_CONFIG_250KBITS(); // 송신부와 동일하게 250Kbps로 설정
    twai_filter_config_t f_config = TWAI_FILTER_CONFIG_ACCEPT_ALL();

    ESP_LOGI(TAG, "Installing TWAI driver...");
    if (twai_driver_install(&g_config, &t_config, &f_config) == ESP_OK) {
        ESP_LOGI(TAG, "Driver installed");
        if (twai_start() == ESP_OK) {
            ESP_LOGI(TAG, "TWAI Driver started");
            xTaskCreatePinnedToCore(twai_receive_task, "TWAI_RX", 4096, NULL, 5, NULL, 1);
            xTaskCreatePinnedToCore(button_task, "BTN_TASK", 2048, NULL, 4, NULL, 1);
        }
    } else {
        ESP_LOGE(TAG, "Failed to install TWAI driver");
    }

    ESP_LOGI(TAG, "Initializing RGB display (ILI6485, 480x272)");

    esp_lcd_rgb_panel_config_t panel_config = {};
    panel_config.data_width = 16;
    panel_config.bits_per_pixel = 16;
    panel_config.num_fbs = 1; // PSRAM 대역폭 보호를 위해 싱글 버퍼 사용
    panel_config.flags.fb_in_psram = 1;
    panel_config.bounce_buffer_size_px = 20 * EXAMPLE_LCD_H_RES; // 바운스 버퍼를 20줄(32KB)로 늘려 EDMA 기아 현상 방지
    panel_config.clk_src = LCD_CLK_SRC_DEFAULT;
    panel_config.disp_gpio_num = EXAMPLE_PIN_NUM_DISP_EN;
    panel_config.pclk_gpio_num = EXAMPLE_PIN_NUM_PCLK;
    panel_config.vsync_gpio_num = EXAMPLE_PIN_NUM_VSYNC;
    panel_config.hsync_gpio_num = EXAMPLE_PIN_NUM_HSYNC;
    panel_config.de_gpio_num = EXAMPLE_PIN_NUM_DE;
    
    // Map 16-bit RGB565 to the RGB lines.
    panel_config.data_gpio_nums[0] = EXAMPLE_PIN_NUM_B3;
    panel_config.data_gpio_nums[1] = EXAMPLE_PIN_NUM_B4;
    panel_config.data_gpio_nums[2] = EXAMPLE_PIN_NUM_B5;
    panel_config.data_gpio_nums[3] = EXAMPLE_PIN_NUM_B6;
    panel_config.data_gpio_nums[4] = EXAMPLE_PIN_NUM_B7;
    
    panel_config.data_gpio_nums[5] = EXAMPLE_PIN_NUM_G2;
    panel_config.data_gpio_nums[6] = EXAMPLE_PIN_NUM_G3;
    panel_config.data_gpio_nums[7] = EXAMPLE_PIN_NUM_G4;
    panel_config.data_gpio_nums[8] = EXAMPLE_PIN_NUM_G5;
    panel_config.data_gpio_nums[9] = EXAMPLE_PIN_NUM_G6;
    panel_config.data_gpio_nums[10] = EXAMPLE_PIN_NUM_G7;
    
    panel_config.data_gpio_nums[11] = EXAMPLE_PIN_NUM_R3;
    panel_config.data_gpio_nums[12] = EXAMPLE_PIN_NUM_R4;
    panel_config.data_gpio_nums[13] = EXAMPLE_PIN_NUM_R5;
    panel_config.data_gpio_nums[14] = EXAMPLE_PIN_NUM_R6;
    panel_config.data_gpio_nums[15] = EXAMPLE_PIN_NUM_R7;

    // Timings from QT-5000H40R79L-6N5W12 Datasheet
    panel_config.timings.pclk_hz = EXAMPLE_LCD_PIXEL_CLOCK_HZ;
    panel_config.timings.h_res = EXAMPLE_LCD_H_RES;
    panel_config.timings.v_res = EXAMPLE_LCD_V_RES;
    
    // 화면 밀림(Wrap-around) 해결을 위해 데이터시트 Typ 값으로 원복
    panel_config.timings.hsync_back_porch = 8;
    panel_config.timings.hsync_front_porch = 4;
    panel_config.timings.hsync_pulse_width = 4;

    panel_config.timings.vsync_back_porch = 8;
    panel_config.timings.vsync_front_porch = 4;
    panel_config.timings.vsync_pulse_width = 4;

    panel_config.timings.flags.pclk_active_neg = true; // PCLK 극성 원복 (화면 지글링 완벽 해결)

    ESP_ERROR_CHECK(esp_lcd_new_rgb_panel(&panel_config, &panel_handle));
    ESP_ERROR_CHECK(esp_lcd_panel_reset(panel_handle));
    ESP_ERROR_CHECK(esp_lcd_panel_init(panel_handle));

    // LVGL Initialization
    lv_init();
    
    // LVGL 렌더링 버퍼를 PSRAM이 아닌 '내부 고속 SRAM(INTERNAL)'에 할당하여 PSRAM 병목 원천 차단
    // 800 * 40 = 32000 pixels = 64KB (ESP32-S3 내부 RAM에 충분히 들어가는 크기)
    size_t draw_buf_sz = EXAMPLE_LCD_H_RES * 40;
    buf1 = (lv_color_t *)heap_caps_malloc(draw_buf_sz * sizeof(lv_color_t), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    buf2 = (lv_color_t *)heap_caps_malloc(draw_buf_sz * sizeof(lv_color_t), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    assert(buf1 && buf2);
    lv_disp_draw_buf_init(&disp_buf, buf1, buf2, draw_buf_sz);

    static lv_disp_drv_t disp_drv;
    lv_disp_drv_init(&disp_drv);
    disp_drv.hor_res = EXAMPLE_LCD_H_RES;
    disp_drv.ver_res = EXAMPLE_LCD_V_RES;
    disp_drv.flush_cb = example_lvgl_flush_cb;
    disp_drv.draw_buf = &disp_buf;
    
    // Direct Mode 및 Full Refresh 해제 (변경된 부분만 렌더링하여 연산량 90% 감소)
    disp_drv.user_data = panel_handle;
    lv_disp_drv_register(&disp_drv);

    const esp_timer_create_args_t lvgl_tick_timer_args = {
        .callback = &example_increase_lvgl_tick,
        .arg = NULL,
        .dispatch_method = ESP_TIMER_TASK,
        .name = "lvgl_tick",
        .skip_unhandled_events = true
    };
    esp_timer_handle_t lvgl_tick_timer = NULL;
    ESP_ERROR_CHECK(esp_timer_create(&lvgl_tick_timer_args, &lvgl_tick_timer));
    ESP_ERROR_CHECK(esp_timer_start_periodic(lvgl_tick_timer, 1000));

    create_dashboard_ui();

    xTaskCreatePinnedToCore(lvgl_port_task, "LVGL", LVGL_TASK_STACK_SIZE, NULL, LVGL_TASK_PRIORITY, NULL, 1);
}
