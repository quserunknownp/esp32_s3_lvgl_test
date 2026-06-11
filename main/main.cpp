#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_lcd_panel_rgb.h"
#include "esp_lcd_panel_ops.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "lvgl.h"

static const char *TAG = "LVGL_RGB_Test";

// --- LCD Specification ---
#define EXAMPLE_LCD_H_RES              800
#define EXAMPLE_LCD_V_RES              480
#define EXAMPLE_LCD_PIXEL_CLOCK_HZ     (25 * 1000 * 1000) // 25 MHz from QT-5000H40R79L datasheet

// ESP32-S3-N16R8 Development Board RGB GPIO configuration
// Pinmap based on C:\proj\01_s3_dashboard\pinmap.txt

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
#define LVGL_TASK_MAX_DELAY_MS 500
#define LVGL_TASK_MIN_DELAY_MS 1
#define LVGL_TASK_STACK_SIZE   (4 * 1024)
#define LVGL_TASK_PRIORITY     2

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
    
    // Pass the draw buffer to the RGB driver
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
        task_delay_ms = lv_timer_handler();
        if (task_delay_ms > LVGL_TASK_MAX_DELAY_MS) {
            task_delay_ms = LVGL_TASK_MAX_DELAY_MS;
        } else if (task_delay_ms < LVGL_TASK_MIN_DELAY_MS) {
            task_delay_ms = LVGL_TASK_MIN_DELAY_MS;
        }
        vTaskDelay(pdMS_TO_TICKS(task_delay_ms));
    }
}

// --- LVGL Dashboard UI (WoB) ---
static lv_obj_t * rpm_label_ptr = NULL;

static void set_arc_value(void * obj, int32_t v)
{
    lv_arc_set_value((lv_obj_t *)obj, v);
    if(rpm_label_ptr) {
        lv_label_set_text_fmt(rpm_label_ptr, "%d", (int)v);
    }
}

void create_dashboard_ui(void)
{
    // 1. Theme: Black Background
    lv_obj_set_style_bg_color(lv_scr_act(), lv_color_black(), 0);

    // 2. RPM Arc (Center/Top 120-degree Arc)
    lv_obj_t * arc = lv_arc_create(lv_scr_act());
    lv_arc_set_rotation(arc, 210);
    lv_arc_set_bg_angles(arc, 0, 120);
    lv_arc_set_range(arc, 0, 12000);
    lv_obj_set_size(arc, 550, 550);
    lv_obj_align(arc, LV_ALIGN_CENTER, 0, 80); // Shift center slightly less
    lv_obj_remove_style(arc, NULL, LV_PART_KNOB);
    lv_obj_clear_flag(arc, LV_OBJ_FLAG_CLICKABLE);
    
    // WoB styling for Arc
    lv_obj_set_style_arc_color(arc, lv_color_hex(0x222222), LV_PART_MAIN); // Dark gray track
    lv_obj_set_style_arc_color(arc, lv_color_white(), LV_PART_INDICATOR);  // White fill
    lv_obj_set_style_arc_width(arc, 45, LV_PART_MAIN);
    lv_obj_set_style_arc_width(arc, 45, LV_PART_INDICATOR);

    // 3. RPM Value Label (Center)
    rpm_label_ptr = lv_label_create(lv_scr_act());
    lv_label_set_text(rpm_label_ptr, "0");
    lv_obj_set_style_text_color(rpm_label_ptr, lv_color_white(), 0);
    lv_obj_set_style_text_font(rpm_label_ptr, &lv_font_montserrat_48, 0);
    lv_obj_align(rpm_label_ptr, LV_ALIGN_CENTER, 0, -30);

    lv_obj_t * rpm_sub = lv_label_create(lv_scr_act());
    lv_label_set_text(rpm_sub, "RPM");
    lv_obj_set_style_text_color(rpm_sub, lv_color_hex(0xAAAAAA), 0);
    lv_obj_set_style_text_font(rpm_sub, &lv_font_montserrat_24, 0);
    lv_obj_align_to(rpm_sub, rpm_label_ptr, LV_ALIGN_OUT_BOTTOM_MID, 0, 10);

    // 4. Temperature Candle & Label (Left Side)
    lv_obj_t * temp_bar = lv_bar_create(lv_scr_act());
    lv_obj_set_size(temp_bar, 50, 260);
    lv_obj_align(temp_bar, LV_ALIGN_LEFT_MID, 50, -30);
    lv_bar_set_range(temp_bar, 20, 120);
    lv_bar_set_value(temp_bar, 85, LV_ANIM_ON);
    lv_obj_set_style_bg_color(temp_bar, lv_color_hex(0x222222), LV_PART_MAIN);
    lv_obj_set_style_bg_color(temp_bar, lv_color_hex(0xFF4444), LV_PART_INDICATOR);

    lv_obj_t * temp_label = lv_label_create(lv_scr_act());
    lv_label_set_text(temp_label, "85 C");
    lv_obj_set_style_text_color(temp_label, lv_color_white(), 0);
    lv_obj_set_style_text_font(temp_label, &lv_font_montserrat_24, 0);
    lv_obj_align_to(temp_label, temp_bar, LV_ALIGN_OUT_BOTTOM_MID, 0, 10);

    // 5. SoC Candle & Label (Right Side)
    lv_obj_t * soc_bar = lv_bar_create(lv_scr_act());
    lv_obj_set_size(soc_bar, 50, 260);
    lv_obj_align(soc_bar, LV_ALIGN_RIGHT_MID, -50, -30);
    lv_bar_set_range(soc_bar, 0, 100);
    lv_bar_set_value(soc_bar, 92, LV_ANIM_ON);
    lv_obj_set_style_bg_color(soc_bar, lv_color_hex(0x222222), LV_PART_MAIN);
    lv_obj_set_style_bg_color(soc_bar, lv_color_hex(0x44FF44), LV_PART_INDICATOR);

    lv_obj_t * soc_label = lv_label_create(lv_scr_act());
    lv_label_set_text(soc_label, "92 %");
    lv_obj_set_style_text_color(soc_label, lv_color_white(), 0);
    lv_obj_set_style_text_font(soc_label, &lv_font_montserrat_24, 0);
    lv_obj_align_to(soc_label, soc_bar, LV_ALIGN_OUT_BOTTOM_MID, 0, 10);

    // 6. Inverter Temp Bar (Bottom Center)
    lv_obj_t * inv_bar = lv_bar_create(lv_scr_act());
    lv_obj_set_size(inv_bar, 300, 30);
    lv_obj_align(inv_bar, LV_ALIGN_BOTTOM_MID, 0, -50);
    lv_bar_set_range(inv_bar, 20, 120);
    lv_bar_set_value(inv_bar, 45, LV_ANIM_ON);
    lv_obj_set_style_bg_color(inv_bar, lv_color_hex(0x222222), LV_PART_MAIN);
    lv_obj_set_style_bg_color(inv_bar, lv_color_hex(0xFF9900), LV_PART_INDICATOR); // Orange

    lv_obj_t * inv_label = lv_label_create(lv_scr_act());
    lv_label_set_text(inv_label, "INV 45 C");
    lv_obj_set_style_text_color(inv_label, lv_color_white(), 0);
    lv_obj_set_style_text_font(inv_label, &lv_font_montserrat_24, 0);
    lv_obj_align_to(inv_label, inv_bar, LV_ALIGN_OUT_BOTTOM_MID, 0, 10);

    // 6. Animation to simulate revving
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, arc);
    lv_anim_set_exec_cb(&a, set_arc_value);
    lv_anim_set_time(&a, 1500);
    lv_anim_set_playback_time(&a, 800);
    lv_anim_set_repeat_count(&a, LV_ANIM_REPEAT_INFINITE);
    lv_anim_set_values(&a, 1000, 11500);
    lv_anim_start(&a);
}

extern "C" void app_main(void)
{
    ESP_LOGI(TAG, "Initializing RGB display (ILI6485, 480x272)");

    esp_lcd_rgb_panel_config_t panel_config = {};
    panel_config.data_width = 16;
    panel_config.bits_per_pixel = 16;
    panel_config.num_fbs = 2;
    panel_config.flags.fb_in_psram = 1;
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
    
    panel_config.timings.hsync_back_porch = 4;
    panel_config.timings.hsync_front_porch = 8;
    panel_config.timings.hsync_pulse_width = 4;

    panel_config.timings.vsync_back_porch = 4;
    panel_config.timings.vsync_front_porch = 8;
    panel_config.timings.vsync_pulse_width = 4;

    panel_config.timings.flags.pclk_active_neg = true; // DCLK falling edge

    ESP_ERROR_CHECK(esp_lcd_new_rgb_panel(&panel_config, &panel_handle));
    ESP_ERROR_CHECK(esp_lcd_panel_reset(panel_handle));
    ESP_ERROR_CHECK(esp_lcd_panel_init(panel_handle));

    // LVGL Initialization
    lv_init();
    
    // Allocate LVGL draw buffers from PSRAM using full screen size
    size_t draw_buf_sz = EXAMPLE_LCD_H_RES * EXAMPLE_LCD_V_RES;
    buf1 = (lv_color_t *)heap_caps_malloc(draw_buf_sz * sizeof(lv_color_t), MALLOC_CAP_DMA | MALLOC_CAP_SPIRAM);
    buf2 = (lv_color_t *)heap_caps_malloc(draw_buf_sz * sizeof(lv_color_t), MALLOC_CAP_DMA | MALLOC_CAP_SPIRAM);
    assert(buf1 && buf2);
    lv_disp_draw_buf_init(&disp_buf, buf1, buf2, draw_buf_sz);

    static lv_disp_drv_t disp_drv;
    lv_disp_drv_init(&disp_drv);
    disp_drv.hor_res = EXAMPLE_LCD_H_RES;
    disp_drv.ver_res = EXAMPLE_LCD_V_RES;
    disp_drv.flush_cb = example_lvgl_flush_cb;
    disp_drv.draw_buf = &disp_buf;
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
