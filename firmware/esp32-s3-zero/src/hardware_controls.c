#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_timer.h"
#include "tinyusb.h"
#include "tusb.h"
#include "class/cdc/cdc_device.h"

#include "board_pins.h"
#include "usb_hid_trigger.h"

static const char *TAG = "hardware_controls";

#define JOY_DEADZONE_COUNTS     120
#define JOY_SAMPLE_AVG          8
#define GIMBAL_MIN_US           1000
#define GIMBAL_MAX_US           2000
#define GIMBAL_CENTER_US        1500

static adc_oneshot_unit_handle_t s_adc1_handle = NULL;
static int s_center_x = 2048;
static int s_center_y = 2048;

static volatile control_mode_t s_current_mode = CONTROL_MODE_DRIVE;
static int s_pan_us = GIMBAL_CENTER_US;
static int s_tilt_us = GIMBAL_CENTER_US;

static int8_t s_last_vx = 0;
static int8_t s_last_vy = 0;
static uint8_t s_stop_burst_count = 0;
static uint8_t s_last_enc_state = 0;
static int8_t s_enc_accum = 0;

static const int8_t s_enc_transitions[16] = {
     0,  1, -1,  0,
    -1,  0,  0,  1,
     1,  0,  0, -1,
     0, -1,  1,  0
};

esp_err_t hardware_controls_init(void)
{
    ESP_LOGI(TAG, "Initializing Hardware Controls (Joystick ADC1: VRx=GP%d, VRy=GP%d, SW=GP%d | Encoder: A=GP%d, B=GP%d)",
             BOARD_PIN_JOY_VRX, BOARD_PIN_JOY_VRY, BOARD_PIN_JOY_SW,
             BOARD_PIN_ENC_A, BOARD_PIN_ENC_B);

    // 1. Initialize ADC1 for 2-axis Joystick
    adc_oneshot_unit_init_cfg_t init_cfg = {
        .unit_id = ADC_UNIT_1,
        .ulp_mode = ADC_ULP_MODE_DISABLE,
    };
    esp_err_t ret = adc_oneshot_new_unit(&init_cfg, &s_adc1_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create ADC1 unit handle: %s", esp_err_to_name(ret));
        return ret;
    }

    adc_oneshot_chan_cfg_t chan_cfg = {
        .bitwidth = ADC_BITWIDTH_DEFAULT,
        .atten = ADC_ATTEN_DB_12,
    };
    ESP_ERROR_CHECK(adc_oneshot_config_channel(s_adc1_handle, ADC_CHANNEL_1, &chan_cfg)); // GPIO 2
    ESP_ERROR_CHECK(adc_oneshot_config_channel(s_adc1_handle, ADC_CHANNEL_2, &chan_cfg)); // GPIO 3

    // 2. Calibrate Joystick Center Offset
    int sum_x = 0, sum_y = 0;
    for (int i = 0; i < 16; i++) {
        int raw_x = 0, raw_y = 0;
        adc_oneshot_read(s_adc1_handle, ADC_CHANNEL_1, &raw_x);
        adc_oneshot_read(s_adc1_handle, ADC_CHANNEL_2, &raw_y);
        sum_x += raw_x;
        sum_y += raw_y;
        vTaskDelay(pdMS_TO_TICKS(5));
    }
    s_center_x = sum_x / 16;
    s_center_y = sum_y / 16;
    ESP_LOGI(TAG, "Joystick Calibrated: Center X = %d, Center Y = %d", s_center_x, s_center_y);

    // 3. Configure Rotary Encoder GPIOs (Internal Pull-Up)
    gpio_config_t enc_cfg = {
        .pin_bit_mask = (1ULL << BOARD_PIN_ENC_A) | (1ULL << BOARD_PIN_ENC_B),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_ERROR_CHECK(gpio_config(&enc_cfg));

    uint8_t a = gpio_get_level((gpio_num_t)BOARD_PIN_ENC_A);
    uint8_t b = gpio_get_level((gpio_num_t)BOARD_PIN_ENC_B);
    s_last_enc_state = (a << 1) | b;

    // 4. Configure Joystick SW Click Pin (GPIO 1, Pull-Up)
    gpio_config_t sw_cfg = {
        .pin_bit_mask = (1ULL << BOARD_PIN_JOY_SW),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_ERROR_CHECK(gpio_config(&sw_cfg));

    ws2812_set_mode(LED_MODE_DRIVE);
    return ESP_OK;
}

control_mode_t hardware_controls_get_mode(void)
{
    return s_current_mode;
}

void hardware_controls_get_gimbal(int *pan_us, int *tilt_us)
{
    if (pan_us) *pan_us = s_pan_us;
    if (tilt_us) *tilt_us = s_tilt_us;
}

static void read_joystick_normalized(int8_t *norm_x, int8_t *norm_y)
{
    int raw_x = 0, raw_y = 0;
    for (int i = 0; i < JOY_SAMPLE_AVG; i++) {
        int x = 0, y = 0;
        adc_oneshot_read(s_adc1_handle, ADC_CHANNEL_1, &x);
        adc_oneshot_read(s_adc1_handle, ADC_CHANNEL_2, &y);
        raw_x += x;
        raw_y += y;
    }
    raw_x /= JOY_SAMPLE_AVG;
    raw_y /= JOY_SAMPLE_AVG;

    int diff_x = raw_x - s_center_x;
    int diff_y = raw_y - s_center_y;

    int out_x = 0;
    if (abs(diff_x) > JOY_DEADZONE_COUNTS) {
        if (diff_x > 0) {
            out_x = ((diff_x - JOY_DEADZONE_COUNTS) * 100) / (4095 - s_center_x - JOY_DEADZONE_COUNTS + 1);
        } else {
            out_x = ((diff_x + JOY_DEADZONE_COUNTS) * 100) / (s_center_x - JOY_DEADZONE_COUNTS + 1);
        }
    }

    int out_y = 0;
    if (abs(diff_y) > JOY_DEADZONE_COUNTS) {
        if (diff_y > 0) {
            out_y = ((diff_y - JOY_DEADZONE_COUNTS) * 100) / (4095 - s_center_y - JOY_DEADZONE_COUNTS + 1);
        } else {
            out_y = ((diff_y + JOY_DEADZONE_COUNTS) * 100) / (s_center_y - JOY_DEADZONE_COUNTS + 1);
        }
    }

    if (out_x > 100) out_x = 100;
    if (out_x < -100) out_x = -100;
    if (out_y > 100) out_y = 100;
    if (out_y < -100) out_y = -100;

    *norm_x = (int8_t)out_x;
    *norm_y = (int8_t)out_y;
}

static void hardware_controls_task(void *pvParameters)
{
    ESP_LOGI(TAG, "Hardware controls real-time loop active on Core 0");

    bool last_sw_pressed = false;
    uint32_t press_start_ms = 0;
    uint32_t last_click_ms = 0;
    bool ptt_active_via_sw = false;

    TickType_t last_wake = xTaskGetTickCount();
    uint32_t loop_counter = 0;

    while (1) {
        // Run loop at 200 Hz (5ms) for rotary encoder and button responsiveness
        vTaskDelayUntil(&last_wake, pdMS_TO_TICKS(5));
        loop_counter++;

        // 1. Poll Rotary Encoder (Quadrature Gray-Code Decoder)
        uint8_t a = gpio_get_level((gpio_num_t)BOARD_PIN_ENC_A);
        uint8_t b = gpio_get_level((gpio_num_t)BOARD_PIN_ENC_B);
        uint8_t enc_state = (a << 1) | b;
        if (enc_state != s_last_enc_state) {
            int8_t step = s_enc_transitions[(s_last_enc_state << 2) | enc_state];
            s_last_enc_state = enc_state;
            s_enc_accum += step;

            // 4 transitions per physical detent click
            if (s_enc_accum >= 4) {
                usb_hid_send_mouse_scroll(1); // Scroll Up
                s_enc_accum = 0;
            } else if (s_enc_accum <= -4) {
                usb_hid_send_mouse_scroll(-1); // Scroll Down
                s_enc_accum = 0;
            }
        }

        // 2. Poll Joystick Click SW (GPIO 1)
        int sw_val = gpio_get_level((gpio_num_t)BOARD_PIN_JOY_SW);
        bool sw_pressed = (sw_val == 0);
        uint32_t now_ms = (uint32_t)(esp_timer_get_time() / 1000);

        if (sw_pressed && !last_sw_pressed) {
            // Button pressed down
            press_start_ms = now_ms;
            last_sw_pressed = true;
        } else if (sw_pressed && last_sw_pressed) {
            // Held down: check long press for PTT (> 450ms)
            if (!ptt_active_via_sw && (now_ms - press_start_ms > 450)) {
                ptt_active_via_sw = true;
                usb_hid_send_ptt_keystroke(true);
                ws2812_set_mode(LED_MODE_PTT);
                ESP_LOGI(TAG, "Joystick SW Long Press -> PTT Start");
            }
        } else if (!sw_pressed && last_sw_pressed) {
            // Button released
            last_sw_pressed = false;
            if (ptt_active_via_sw) {
                ptt_active_via_sw = false;
                usb_hid_send_ptt_keystroke(false);
                ws2812_set_mode(s_current_mode == CONTROL_MODE_DRIVE ? LED_MODE_DRIVE : LED_MODE_GIMBAL);
                ESP_LOGI(TAG, "Joystick SW Released -> PTT Stop");
            } else {
                // Short click (< 450ms)
                if (now_ms - last_click_ms < 350) {
                    // Double Click: Center Gimbal
                    // Restore original mode since single click previously toggled it
                    s_current_mode = (s_current_mode == CONTROL_MODE_DRIVE) ? CONTROL_MODE_GIMBAL : CONTROL_MODE_DRIVE;
                    ws2812_set_mode(s_current_mode == CONTROL_MODE_DRIVE ? LED_MODE_DRIVE : LED_MODE_GIMBAL);
                    s_pan_us = GIMBAL_CENTER_US;
                    s_tilt_us = GIMBAL_CENTER_US;
                    ESP_LOGI(TAG, "Joystick SW Double-Click -> Gimbal Reset (1500, 1500), Mode restored to %s",
                             s_current_mode == CONTROL_MODE_DRIVE ? "DRIVE" : "GIMBAL");
                    if (tud_cdc_connected()) {
                        char mode_buf[48];
                        snprintf(mode_buf, sizeof(mode_buf), "ROVER_MODE:%s\n",
                                 s_current_mode == CONTROL_MODE_DRIVE ? "DRIVE" : "GIMBAL");
                        tud_cdc_write_str(mode_buf);
                        char buf[64];
                        snprintf(buf, sizeof(buf), "CMD:ROVER_GIMBAL %d %d\n", s_pan_us, s_tilt_us);
                        tud_cdc_write_str(buf);
                        snprintf(buf, sizeof(buf), "G,%d,%d\n", s_pan_us, s_tilt_us);
                        tud_cdc_write_str(buf);
                        tud_cdc_write_flush();
                    }
                    last_click_ms = 0;
                } else {
                    // Single Click: Toggle Mode
                    last_click_ms = now_ms;
                    s_current_mode = (s_current_mode == CONTROL_MODE_DRIVE) ? CONTROL_MODE_GIMBAL : CONTROL_MODE_DRIVE;
                    ws2812_set_mode(s_current_mode == CONTROL_MODE_DRIVE ? LED_MODE_DRIVE : LED_MODE_GIMBAL);
                    ESP_LOGI(TAG, "Joystick SW Click -> Mode: %s",
                             s_current_mode == CONTROL_MODE_DRIVE ? "DRIVE" : "GIMBAL");
                    if (tud_cdc_connected()) {
                        char buf[48];
                        snprintf(buf, sizeof(buf), "ROVER_MODE:%s\n",
                                 s_current_mode == CONTROL_MODE_DRIVE ? "DRIVE" : "GIMBAL");
                        tud_cdc_write_str(buf);
                        tud_cdc_write_flush();
                    }
                }
            }
        }

        // 3. Periodic Joystick Sampling & Command Dispatch (every 30ms / ~33 Hz)
        if ((loop_counter % 6) == 0) {
            int8_t norm_x = 0, norm_y = 0;
            read_joystick_normalized(&norm_x, &norm_y);

            if (s_current_mode == CONTROL_MODE_DRIVE) {
                // In Drive Mode:
                // norm_y = Forward/Reverse (vx)
                // norm_x = Lateral Strafe (vy)
                int8_t vx = norm_y;
                int8_t vy = norm_x;
                int8_t omega = 0;

                bool is_moving = (vx != 0 || vy != 0);
                bool was_moving = (s_last_vx != 0 || s_last_vy != 0);

                if (was_moving && !is_moving) {
                    s_stop_burst_count = 4; // Send 4 burst packets to guarantee stop arrival
                }

                if (is_moving || s_stop_burst_count > 0) {
                    espnow_rover_send_command(vx, vy, omega, 0, 0);

                    if (tud_cdc_connected()) {
                        char cdc_buf[64];
                        snprintf(cdc_buf, sizeof(cdc_buf), "CMD:ROVER_DRIVE %d %d %d 0 0\n", vx, vy, omega);
                        tud_cdc_write_str(cdc_buf);
                        tud_cdc_write_flush();
                    }

                    if (!is_moving && s_stop_burst_count > 0) {
                        s_stop_burst_count--;
                    }
                }

                s_last_vx = vx;
                s_last_vy = vy;
            } else if (s_current_mode == CONTROL_MODE_GIMBAL) {
                // In Gimbal Pan/Tilt Mode:
                // norm_x controls Pan angle slew (-100..100)
                // norm_y controls Tilt angle slew (-100..100)
                if (norm_x != 0 || norm_y != 0) {
                    int delta_pan = (norm_x * 10) / 100;
                    int delta_tilt = (norm_y * 10) / 100;

                    s_pan_us += delta_pan;
                    s_tilt_us += delta_tilt;

                    if (s_pan_us < GIMBAL_MIN_US) s_pan_us = GIMBAL_MIN_US;
                    if (s_pan_us > GIMBAL_MAX_US) s_pan_us = GIMBAL_MAX_US;
                    if (s_tilt_us < GIMBAL_MIN_US) s_tilt_us = GIMBAL_MIN_US;
                    if (s_tilt_us > GIMBAL_MAX_US) s_tilt_us = GIMBAL_MAX_US;

                    if (tud_cdc_connected()) {
                        char buf[64];
                        snprintf(buf, sizeof(buf), "CMD:ROVER_GIMBAL %d %d\n", s_pan_us, s_tilt_us);
                        tud_cdc_write_str(buf);
                        snprintf(buf, sizeof(buf), "G,%d,%d\n", s_pan_us, s_tilt_us);
                        tud_cdc_write_str(buf);
                        tud_cdc_write_flush();
                    }
                }
            }
        }
    }
}

void hardware_controls_start_task(void)
{
    xTaskCreatePinnedToCore(hardware_controls_task, "hw_ctrl", 3072, NULL, 5, NULL, 0);
}
