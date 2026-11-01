#include <stdio.h>
#include <math.h>
#include <string.h>
#include "esp_log.h"
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "led_strip.h"

#include "board_pins.h"
#include "ringlight_driver.h"

static const char *TAG = "ringlight_driver";

static led_strip_handle_t s_ring_strip = NULL;
static led_strip_handle_t s_onboard_strip = NULL;
static portMUX_TYPE s_ring_lock = portMUX_INITIALIZER_UNLOCKED;

static volatile uint8_t s_pattern_id = RINGLIGHT_PATTERN_COSMIC_NEBULA;
static volatile int16_t s_hue_offset = 0;
static volatile uint16_t s_speed_pct = 100;
static volatile uint8_t s_audio_energy = 0;
static volatile uint16_t s_wake_flash_ms = 0;
static volatile uint8_t s_brightness_pct = 100;

static void hsv_to_rgb(float h, float s, float v, uint8_t *r, uint8_t *g, uint8_t *b)
{
    while (h < 0.0f) h += 360.0f;
    while (h >= 360.0f) h -= 360.0f;

    float c = v * s;
    float h_prime = h / 60.0f;
    float x = c * (1.0f - fabsf(fmodf(h_prime, 2.0f) - 1.0f));
    float m = v - c;

    float r1 = 0, g1 = 0, b1 = 0;
    if (h_prime >= 0 && h_prime < 1) {
        r1 = c; g1 = x; b1 = 0;
    } else if (h_prime >= 1 && h_prime < 2) {
        r1 = x; g1 = c; b1 = 0;
    } else if (h_prime >= 2 && h_prime < 3) {
        r1 = 0; g1 = c; b1 = x;
    } else if (h_prime >= 3 && h_prime < 4) {
        r1 = 0; g1 = x; b1 = c;
    } else if (h_prime >= 4 && h_prime < 5) {
        r1 = x; g1 = 0; b1 = c;
    } else {
        r1 = c; g1 = 0; b1 = x;
    }

    *r = (uint8_t)roundf((r1 + m) * 255.0f);
    *g = (uint8_t)roundf((g1 + m) * 255.0f);
    *b = (uint8_t)roundf((b1 + m) * 255.0f);
}

static void ringlight_render_task(void *pvParameters)
{
    ESP_LOGI(TAG, "Ringlight 16-LED real-time shader task active (40 FPS, GP%d & GP%d)",
             BOARD_PIN_RINGLIGHT, BOARD_PIN_WS2812);
    float phase = 0.0f;
    TickType_t last_wake = xTaskGetTickCount();

    while (1) {
        vTaskDelayUntil(&last_wake, pdMS_TO_TICKS(25)); // 40 FPS

        taskENTER_CRITICAL(&s_ring_lock);
        uint8_t pat = s_pattern_id;
        int16_t hue_off = s_hue_offset;
        uint16_t spd = s_speed_pct;
        uint8_t energy = s_audio_energy;
        uint16_t flash_ms = s_wake_flash_ms;
        float bright = (float)s_brightness_pct / 100.0f;
        if (s_wake_flash_ms >= 25) {
            s_wake_flash_ms -= 25;
        } else {
            s_wake_flash_ms = 0;
        }
        taskEXIT_CRITICAL(&s_ring_lock);

        float phase_inc = (0.05f * (float)spd) / 100.0f;
        phase += phase_inc;
        if (phase > 2000.0f * M_PI) phase -= 2000.0f * M_PI;

        uint8_t ring_r[BOARD_RINGLIGHT_LEDS] = {0};
        uint8_t ring_g[BOARD_RINGLIGHT_LEDS] = {0};
        uint8_t ring_b[BOARD_RINGLIGHT_LEDS] = {0};

        if (flash_ms > 0) {
            // Wake flash effect: full white pulse across all LEDs
            float intensity = (float)flash_ms / 400.0f;
            uint8_t val = (uint8_t)(intensity * 255.0f * bright);
            for (int i = 0; i < BOARD_RINGLIGHT_LEDS; i++) {
                ring_r[i] = val;
                ring_g[i] = val;
                ring_b[i] = val;
            }
        } else {
            for (int i = 0; i < BOARD_RINGLIGHT_LEDS; i++) {
                float led_angle = (float)i * (360.0f / (float)BOARD_RINGLIGHT_LEDS);
                float h = 0.0f, s = 1.0f, v = 0.8f * bright;

                switch (pat) {
                    case RINGLIGHT_PATTERN_COSMIC_NEBULA: {
                        // Slow breathing chromatic drift around ring
                        float wave = sinf(phase * 0.5f + ((float)i * 0.392f));
                        h = fmodf(240.0f + 60.0f * wave + (float)hue_off + 3600.0f, 360.0f);
                        s = 0.85f;
                        v = (0.35f + 0.35f * wave) * bright;
                        break;
                    }
                    case RINGLIGHT_PATTERN_CYBERPUNK_AURORA: {
                        // Rotating aurora modulated by audio energy
                        float audio_boost = (float)energy / 255.0f;
                        float wave = sinf(phase + ((float)i * 0.5f));
                        h = fmodf(160.0f + 120.0f * wave + (float)hue_off + 3600.0f, 360.0f);
                        s = 0.9f;
                        v = (0.30f + 0.70f * audio_boost) * bright;
                        break;
                    }
                    case RINGLIGHT_PATTERN_ORBITAL_AMBER: {
                        // Orbital comet chasing clockwise around 16-LED ring
                        float comet_pos = fmodf(phase * 2.5f, (float)BOARD_RINGLIGHT_LEDS);
                        float dist = fabsf((float)i - comet_pos);
                        if (dist > (float)BOARD_RINGLIGHT_LEDS / 2.0f) {
                            dist = (float)BOARD_RINGLIGHT_LEDS - dist;
                        }
                        float intensity = fmaxf(0.0f, 1.0f - (dist / 3.0f));
                        h = fmodf(35.0f + (float)hue_off + 3600.0f, 360.0f);
                        s = 0.95f;
                        v = (0.15f + 0.85f * intensity) * bright;
                        break;
                    }
                    case RINGLIGHT_PATTERN_QUANTUM_WAVE: {
                        // Fast dual interference wave
                        float w1 = sinf(phase * 1.5f + (float)i * 0.785f);
                        float w2 = sinf(-phase * 1.2f + (float)i * 0.785f);
                        h = fmodf(180.0f + 120.0f * (w1 * 0.5f + w2 * 0.5f) + (float)hue_off + 3600.0f, 360.0f);
                        s = 0.95f;
                        v = (0.4f + 0.5f * fabsf(w1 + w2) * 0.5f) * bright;
                        break;
                    }
                    case RINGLIGHT_PATTERN_SOLAR_FLARE: {
                        // Breathing solar gold to crimson flare
                        float pulse = sinf(phase * 1.0f + (float)i * 0.392f);
                        h = fmodf(10.0f + 35.0f * pulse + (float)hue_off + 3600.0f, 360.0f);
                        s = 1.0f;
                        v = (0.4f + 0.55f * pulse) * bright;
                        break;
                    }
                    case RINGLIGHT_PATTERN_DRIVE_EMERALD: {
                        // Emerald chase wave
                        float wave = sinf(phase * 1.2f + (float)i * 0.392f);
                        h = fmodf(135.0f + (float)hue_off + 3600.0f, 360.0f);
                        s = 0.95f;
                        v = (0.3f + 0.65f * fmaxf(0.0f, wave)) * bright;
                        break;
                    }
                    case RINGLIGHT_PATTERN_PTT_ACTIVE: {
                        // Bright solid golden-amber PTT indicator
                        h = 38.0f;
                        s = 1.0f;
                        v = 0.95f * bright;
                        break;
                    }
                    case RINGLIGHT_PATTERN_RNDIS: {
                        // Deep violet pulse
                        float pulse = 0.5f + 0.5f * sinf(phase * 2.0f);
                        h = 280.0f;
                        s = 1.0f;
                        v = (0.3f + 0.7f * pulse) * bright;
                        break;
                    }
                    case RINGLIGHT_PATTERN_ERROR: {
                        // Urgent red strobe
                        float strobe = sinf(phase * 4.0f) > 0.0f ? 1.0f : 0.1f;
                        h = 0.0f;
                        s = 1.0f;
                        v = strobe * bright;
                        break;
                    }
                    default:
                        h = 210.0f;
                        s = 0.8f;
                        v = 0.5f * bright;
                        break;
                }

                hsv_to_rgb(h, s, v, &ring_r[i], &ring_g[i], &ring_b[i]);
            }
        }

        // Push pixels to 16-LED Ringlight
        if (s_ring_strip) {
            for (int i = 0; i < BOARD_RINGLIGHT_LEDS; i++) {
                led_strip_set_pixel(s_ring_strip, i, ring_r[i], ring_g[i], ring_b[i]);
            }
            led_strip_refresh(s_ring_strip);
        }

        // Mirror first pixel to onboard WS2812 for status telemetry
        if (s_onboard_strip) {
            led_strip_set_pixel(s_onboard_strip, 0, ring_r[0], ring_g[0], ring_b[0]);
            led_strip_refresh(s_onboard_strip);
        }
    }
}

esp_err_t ringlight_driver_init(void)
{
    ESP_LOGI(TAG, "Initializing 16-LED Ringlight on GP%d and Onboard LED on GP%d",
             BOARD_PIN_RINGLIGHT, BOARD_PIN_WS2812);

    // 1. Initialize 16-LED Ringlight on GP10 (DI)
    led_strip_config_t ring_cfg = {
        .strip_gpio_num = (gpio_num_t)BOARD_PIN_RINGLIGHT,
        .max_leds = BOARD_RINGLIGHT_LEDS,
        .led_pixel_format = LED_PIXEL_FORMAT_GRB,
        .led_model = LED_MODEL_WS2812,
        .flags.invert_out = false,
    };
    led_strip_rmt_config_t rmt_cfg1 = {
        .clk_src = RMT_CLK_SRC_DEFAULT,
        .resolution_hz = 10 * 1000 * 1000,
        .flags.with_dma = false,
    };

    esp_err_t ret = led_strip_new_rmt_device(&ring_cfg, &rmt_cfg1, &s_ring_strip);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to init 16-LED Ringlight RMT device: %s", esp_err_to_name(ret));
        return ret;
    }
    led_strip_clear(s_ring_strip);

    // 2. Initialize Onboard WS2812 on GP21
    led_strip_config_t onboard_cfg = {
        .strip_gpio_num = (gpio_num_t)BOARD_PIN_WS2812,
        .max_leds = 1,
        .led_pixel_format = LED_PIXEL_FORMAT_GRB,
        .led_model = LED_MODEL_WS2812,
        .flags.invert_out = false,
    };
    led_strip_rmt_config_t rmt_cfg2 = {
        .clk_src = RMT_CLK_SRC_DEFAULT,
        .resolution_hz = 10 * 1000 * 1000,
        .flags.with_dma = false,
    };
    esp_err_t ret2 = led_strip_new_rmt_device(&onboard_cfg, &rmt_cfg2, &s_onboard_strip);
    if (ret2 == ESP_OK) {
        led_strip_clear(s_onboard_strip);
    } else {
        ESP_LOGW(TAG, "Onboard LED init non-fatal: %s", esp_err_to_name(ret2));
    }

    BaseType_t res = xTaskCreatePinnedToCore(
        ringlight_render_task,
        "ringlight_shd",
        3072,
        NULL,
        4,
        NULL,
        0
    );

    return (res == pdPASS) ? ESP_OK : ESP_FAIL;
}

void ringlight_set_pattern(uint8_t pattern_id, int16_t hue_offset, uint16_t speed)
{
    taskENTER_CRITICAL(&s_ring_lock);
    s_pattern_id = pattern_id;
    s_hue_offset = hue_offset;
    if (speed > 0) s_speed_pct = speed;
    taskEXIT_CRITICAL(&s_ring_lock);
}

void ringlight_set_brightness(uint8_t brightness_pct)
{
    if (brightness_pct > 100) brightness_pct = 100;
    taskENTER_CRITICAL(&s_ring_lock);
    s_brightness_pct = brightness_pct;
    taskEXIT_CRITICAL(&s_ring_lock);
}

void ringlight_set_audio_energy(uint8_t energy)
{
    taskENTER_CRITICAL(&s_ring_lock);
    s_audio_energy = energy;
    taskEXIT_CRITICAL(&s_ring_lock);
}

void ringlight_flash_wake(void)
{
    taskENTER_CRITICAL(&s_ring_lock);
    s_wake_flash_ms = 400;
    taskEXIT_CRITICAL(&s_ring_lock);
}

void ringlight_set_mode(led_mode_t mode)
{
    taskENTER_CRITICAL(&s_ring_lock);
    switch (mode) {
        case LED_MODE_IDLE:
            s_pattern_id = RINGLIGHT_PATTERN_COSMIC_NEBULA;
            break;
        case LED_MODE_WAKE:
            s_wake_flash_ms = 400;
            s_pattern_id = RINGLIGHT_PATTERN_CYBERPUNK_AURORA;
            break;
        case LED_MODE_STREAMING:
            s_pattern_id = RINGLIGHT_PATTERN_CYBERPUNK_AURORA;
            break;
        case LED_MODE_PTT:
            s_pattern_id = RINGLIGHT_PATTERN_PTT_ACTIVE;
            break;
        case LED_MODE_RNDIS:
            s_pattern_id = RINGLIGHT_PATTERN_RNDIS;
            break;
        case LED_MODE_DRIVE:
            s_pattern_id = RINGLIGHT_PATTERN_DRIVE_EMERALD;
            break;
        case LED_MODE_GIMBAL:
            s_pattern_id = RINGLIGHT_PATTERN_ORBITAL_AMBER;
            break;
        case LED_MODE_ERROR:
            s_pattern_id = RINGLIGHT_PATTERN_ERROR;
            break;
        default:
            break;
    }
    taskEXIT_CRITICAL(&s_ring_lock);
}
