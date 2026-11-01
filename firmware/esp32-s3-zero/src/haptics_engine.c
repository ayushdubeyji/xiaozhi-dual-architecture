#include <stdio.h>
#include <math.h>
#include <string.h>
#include "esp_log.h"
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/ledc.h"
#include "driver/gpio.h"

#include "board_pins.h"
#include "haptics_engine.h"

static const char *TAG = "haptics_engine";

#define HAPTIC_LEDC_MODE        LEDC_LOW_SPEED_MODE
#define HAPTIC_LEDC_CHANNEL     LEDC_CHANNEL_0
#define HAPTIC_LEDC_TIMER       LEDC_TIMER_0
#define HAPTIC_LEDC_FREQ_HZ     15000
#define HAPTIC_LEDC_RESOLUTION  LEDC_TIMER_8_BIT

typedef struct {
    volatile uint8_t effect;
    volatile uint8_t intensity;
    volatile uint16_t duration_ms;
    volatile uint16_t elapsed_ms;
    volatile bool active;
} haptic_ctrl_t;

static haptic_ctrl_t s_ctrl = {0};
static portMUX_TYPE s_lock = portMUX_INITIALIZER_UNLOCKED;
static TaskHandle_t s_task_handle = NULL;

static void set_pwm_duty(uint8_t duty)
{
    ledc_set_duty(HAPTIC_LEDC_MODE, HAPTIC_LEDC_CHANNEL, duty);
    ledc_update_duty(HAPTIC_LEDC_MODE, HAPTIC_LEDC_CHANNEL);
}

static void haptics_engine_task(void *pvParameters)
{
    ESP_LOGI(TAG, "Haptics 1ms real-time easing loop started on Core 0");
    TickType_t last_wake = xTaskGetTickCount();

    while (1) {
        if (!s_ctrl.active) {
            set_pwm_duty(0);
            ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
            last_wake = xTaskGetTickCount();
        }

        vTaskDelayUntil(&last_wake, pdMS_TO_TICKS(1));

        taskENTER_CRITICAL(&s_lock);
        if (!s_ctrl.active) {
            taskEXIT_CRITICAL(&s_lock);
            continue;
        }

        uint8_t eff = s_ctrl.effect;
        uint8_t base_int = s_ctrl.intensity;
        uint16_t total_t = s_ctrl.duration_ms;
        uint16_t t = s_ctrl.elapsed_ms;
        taskEXIT_CRITICAL(&s_lock);

        if (t >= total_t) {
            taskENTER_CRITICAL(&s_lock);
            s_ctrl.active = false;
            taskEXIT_CRITICAL(&s_lock);
            set_pwm_duty(0);
            continue;
        }

        float factor = 0.0f;
        switch (eff) {
            case HAPTIC_TICK: {
                // Index 1: Sharp exponential decay kick (8ms burst + smooth decay)
                uint16_t burst = (total_t > 8) ? 8 : (total_t / 2);
                if (t < burst) {
                    factor = 1.0f;
                } else {
                    float norm_decay = (float)(t - burst) / (float)(total_t - burst);
                    factor = expf(-3.5f * norm_decay);
                }
                break;
            }

            case HAPTIC_PRESS: {
                // Index 2: Ease-in-quad ramp (t^2, 25ms ramp + hold)
                uint16_t ramp = (total_t > 25) ? 25 : (total_t * 3 / 4);
                if (ramp == 0) ramp = 1;
                if (t <= ramp) {
                    float norm = (float)t / (float)ramp;
                    factor = norm * norm;
                } else {
                    factor = 1.0f;
                }
                break;
            }

            case HAPTIC_RELEASE: {
                // Index 3: Ease-out-cubic return: (1 - t/T)^3
                float norm = (float)t / (float)total_t;
                if (norm > 1.0f) norm = 1.0f;
                float inv = 1.0f - norm;
                factor = inv * inv * inv;
                break;
            }

            case HAPTIC_DETENT: {
                // Index 4: Gaussian bell-curve pulse (12ms)
                float center = (float)total_t / 2.0f;
                float sigma = (float)total_t / 4.0f;
                if (sigma < 1.0f) sigma = 1.0f;
                float diff = ((float)t - center) / sigma;
                factor = expf(-0.5f * diff * diff);
                break;
            }

            case HAPTIC_JOYSTICK_CENTER: {
                // Index 5: Quick 10ms click when entering/leaving center deadzone
                float norm = (float)t / (float)total_t;
                if (norm > 1.0f) norm = 1.0f;
                factor = sinf((float)M_PI * norm);
                break;
            }

            case HAPTIC_ALERT: {
                // Index 6: Dual smooth sine-modulated pulse for E-Stop/warnings
                uint16_t period = total_t / 2;
                if (period == 0) period = 1;
                uint16_t sub_t = t % period;
                float norm = (float)sub_t / (float)period;
                factor = sinf((float)M_PI * norm);
                break;
            }

            default:
                factor = 0.0f;
                break;
        }

        int duty = (int)roundf((float)base_int * factor);
        if (duty < 0) duty = 0;
        if (duty > 255) duty = 255;
        set_pwm_duty((uint8_t)duty);

        taskENTER_CRITICAL(&s_lock);
        s_ctrl.elapsed_ms++;
        taskEXIT_CRITICAL(&s_lock);
    }
}

esp_err_t haptics_engine_init(void)
{
    ESP_LOGI(TAG, "Initializing Haptics Engine on GPIO %d (15 kHz PWM LEDC)", BOARD_PIN_HAPTIC_PWM);

    ledc_timer_config_t timer_conf = {
        .speed_mode = HAPTIC_LEDC_MODE,
        .timer_num = HAPTIC_LEDC_TIMER,
        .duty_resolution = HAPTIC_LEDC_RESOLUTION,
        .freq_hz = HAPTIC_LEDC_FREQ_HZ,
        .clk_cfg = LEDC_AUTO_CLK
    };
    esp_err_t ret = ledc_timer_config(&timer_conf);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to configure LEDC timer: %s", esp_err_to_name(ret));
        return ret;
    }

    ledc_channel_config_t ch_conf = {
        .speed_mode = HAPTIC_LEDC_MODE,
        .channel = HAPTIC_LEDC_CHANNEL,
        .timer_sel = HAPTIC_LEDC_TIMER,
        .intr_type = LEDC_INTR_DISABLE,
        .gpio_num = (gpio_num_t)BOARD_PIN_HAPTIC_PWM,
        .duty = 0,
        .hpoint = 0
    };
    ret = ledc_channel_config(&ch_conf);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to configure LEDC channel: %s", esp_err_to_name(ret));
        return ret;
    }

    set_pwm_duty(0);

    BaseType_t res = xTaskCreatePinnedToCore(
        haptics_engine_task,
        "haptics_1ms",
        2048,
        NULL,
        6,
        &s_task_handle,
        0
    );

    return (res == pdPASS) ? ESP_OK : ESP_FAIL;
}

void haptics_play(haptic_effect_t effect, uint8_t intensity, uint16_t duration_ms)
{
    if (effect < HAPTIC_TICK || effect > HAPTIC_ALERT) return;
    if (intensity == 0) intensity = 180;
    if (duration_ms == 0) {
        switch (effect) {
            case HAPTIC_TICK:            duration_ms = 15;  break;
            case HAPTIC_PRESS:           duration_ms = 35;  break;
            case HAPTIC_RELEASE:         duration_ms = 20;  break;
            case HAPTIC_DETENT:          duration_ms = 12;  break;
            case HAPTIC_JOYSTICK_CENTER: duration_ms = 10;  break;
            case HAPTIC_ALERT:           duration_ms = 240; break;
            default:                     duration_ms = 15;  break;
        }
    }

    taskENTER_CRITICAL(&s_lock);
    s_ctrl.effect = (uint8_t)effect;
    s_ctrl.intensity = intensity;
    s_ctrl.duration_ms = duration_ms;
    s_ctrl.elapsed_ms = 0;
    s_ctrl.active = true;
    taskEXIT_CRITICAL(&s_lock);

    if (s_task_handle) {
        xTaskNotifyGive(s_task_handle);
    }
}

void haptics_stop(void)
{
    taskENTER_CRITICAL(&s_lock);
    s_ctrl.active = false;
    s_ctrl.elapsed_ms = 0;
    taskEXIT_CRITICAL(&s_lock);
    set_pwm_duty(0);
}
