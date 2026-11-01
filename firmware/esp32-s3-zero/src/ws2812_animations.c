#include <stdio.h>
#include <math.h>
#include <string.h>
#include "esp_log.h"
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "led_strip.h"

#include "board_pins.h"
#include "ws2812_animations.h"

static const char *TAG = "ws2812_anim";

static led_strip_handle_t s_strip = NULL;
static portMUX_TYPE s_anim_lock = portMUX_INITIALIZER_UNLOCKED;

static volatile uint8_t s_pattern_id = LED_PATTERN_COSMIC_NEBULA;
static volatile int16_t s_hue_offset = 0;
static volatile uint16_t s_speed_pct = 100;
static volatile uint8_t s_audio_energy = 0;
static volatile uint16_t s_wake_flash_ms = 0;

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

static void ws2812_render_task(void *pvParameters)
{
    ESP_LOGI(TAG, "WS2812 organic shader task started (40 FPS, GPIO %d)", BOARD_PIN_WS2812);
    float phase = 0.0f;
    TickType_t last_wake = xTaskGetTickCount();

    while (1) {
        vTaskDelayUntil(&last_wake, pdMS_TO_TICKS(25)); // 40 FPS

        if (!s_strip) continue;

        taskENTER_CRITICAL(&s_anim_lock);
        uint8_t pid = s_pattern_id;
        int16_t hue_off = s_hue_offset;
        uint16_t speed = s_speed_pct;
        uint8_t audio = s_audio_energy;
        uint16_t wake_flash = s_wake_flash_ms;
        if (s_wake_flash_ms > 25) {
            s_wake_flash_ms -= 25;
        } else {
            s_wake_flash_ms = 0;
        }
        taskEXIT_CRITICAL(&s_anim_lock);

        // Flash override for WakeNet detection
        if (wake_flash > 0) {
            led_strip_set_pixel(s_strip, 0, 0, 255, 255); // Vibrant Cyan Flash
            led_strip_refresh(s_strip);
            continue;
        }

        float speed_scale = (float)speed / 100.0f;
        if (speed_scale < 0.1f) speed_scale = 0.1f;
        phase += 0.04f * speed_scale;
        if (phase > 2000.0f * (float)M_PI) phase = 0.0f;

        float h = 0.0f, s = 1.0f, v = 0.5f;

        switch (pid) {
            case LED_PATTERN_COSMIC_NEBULA: {
                // Idle: Slow organic breathing chromatic drift through nebula purples, cosmic blues, soft emeralds
                // Nebula purple ~280, blue ~220, emerald ~150
                float drift = sinf(0.35f * phase);
                if (drift > 0.0f) {
                    // Drift between blue and purple
                    h = 240.0f + 45.0f * drift;
                } else {
                    // Drift between blue and soft emerald
                    h = 240.0f + 90.0f * drift;
                }
                s = 0.92f;
                // Organic breathing ease-in-out
                float breath = 0.5f * (1.0f - cosf(0.45f * phase));
                v = 0.08f + 0.40f * powf(breath, 1.4f);
                break;
            }

            case LED_PATTERN_CYBERPUNK_AURORA: {
                // Talking to Xiaozhi Agent: Dynamic audio-reactive wave / breathing aurora
                // Shifting cyan (180), electric violet (275), and deep aqua (165)
                float wave = sinf(1.1f * phase);
                h = 220.0f + 55.0f * wave;
                s = 0.96f;

                float breath = 0.5f * (1.0f - cosf(1.4f * phase));
                float audio_boost = (float)audio / 255.0f;
                v = 0.25f + 0.50f * breath + 0.25f * audio_boost;
                if (v > 1.0f) v = 1.0f;
                break;
            }

            case LED_PATTERN_ORBITAL_AMBER: {
                // Gimbal Mode: Warm amber orbital pulse
                // Warm amber ~38-42 deg
                h = 38.0f + 4.0f * sinf(phase * 0.5f);
                s = 0.98f;
                // Orbital pulse: asymmetric sharp surge
                float p = sinf(phase * 1.5f);
                float surge = expf(-5.0f * (p - 0.75f) * (p - 0.75f));
                v = 0.15f + 0.80f * surge;
                break;
            }

            case LED_PATTERN_QUANTUM_WAVE: {
                // Neon magenta (315) and electric cyan (180) rapid interference
                h = 245.0f + 70.0f * sinf(2.0f * phase);
                s = 0.95f;
                float breath = 0.5f * (1.0f - cosf(1.8f * phase));
                v = 0.20f + 0.70f * breath;
                break;
            }

            case LED_PATTERN_SOLAR_FLARE: {
                // Deep crimson (5) to solar gold (45)
                h = 25.0f + 20.0f * sinf(1.2f * phase);
                s = 1.0f;
                float breath = 0.5f * (1.0f - cosf(1.3f * phase));
                v = 0.20f + 0.75f * breath;
                break;
            }

            case LED_PATTERN_DRIVE_EMERALD: {
                // Lush emerald dynamic drive wave
                h = 145.0f + 15.0f * sinf(phase);
                s = 0.95f;
                v = 0.25f + 0.55f * (0.5f * (1.0f - cosf(phase)));
                break;
            }

            case LED_PATTERN_PTT_ACTIVE: {
                // Hardware PTT held: Bright amber glow
                h = 42.0f;
                s = 1.0f;
                v = 0.85f + 0.15f * sinf(6.0f * phase);
                break;
            }

            case LED_PATTERN_RNDIS: {
                // Emergency RNDIS active: Deep electric violet
                h = 280.0f;
                s = 1.0f;
                v = 0.30f + 0.50f * (0.5f * (1.0f - cosf(2.0f * phase)));
                break;
            }

            case LED_PATTERN_ERROR: {
                // System Error: Urgent red
                h = 0.0f;
                s = 1.0f;
                v = (fmodf(phase * 4.0f, 2.0f * (float)M_PI) < (float)M_PI) ? 0.9f : 0.05f;
                break;
            }

            default:
                h = 240.0f;
                s = 1.0f;
                v = 0.3f;
                break;
        }

        h += (float)hue_off;

        uint8_t r = 0, g = 0, b = 0;
        hsv_to_rgb(h, s, v, &r, &g, &b);

        led_strip_set_pixel(s_strip, 0, r, g, b);
        led_strip_refresh(s_strip);
    }
}

esp_err_t ws2812_animations_init(void)
{
    ESP_LOGI(TAG, "Initializing WS2812 LED strip on GPIO %d", BOARD_PIN_WS2812);

    led_strip_config_t strip_config = {
        .strip_gpio_num = (gpio_num_t)BOARD_PIN_WS2812,
        .max_leds = 1,
        .led_pixel_format = LED_PIXEL_FORMAT_GRB,
        .led_model = LED_MODEL_WS2812,
        .flags.invert_out = false,
    };
    led_strip_rmt_config_t rmt_config = {
        .clk_src = RMT_CLK_SRC_DEFAULT,
        .resolution_hz = 10 * 1000 * 1000,
        .flags.with_dma = false,
    };

    esp_err_t ret = led_strip_new_rmt_device(&strip_config, &rmt_config, &s_strip);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to init WS2812 device: %s", esp_err_to_name(ret));
        return ret;
    }

    led_strip_clear(s_strip);

    BaseType_t res = xTaskCreatePinnedToCore(
        ws2812_render_task,
        "ws2812_shader",
        2560,
        NULL,
        4,
        NULL,
        0
    );

    return (res == pdPASS) ? ESP_OK : ESP_FAIL;
}

void ws2812_set_pattern(uint8_t pattern_id, int16_t hue_offset, uint16_t speed)
{
    taskENTER_CRITICAL(&s_anim_lock);
    s_pattern_id = pattern_id;
    s_hue_offset = hue_offset;
    if (speed > 0) s_speed_pct = speed;
    taskEXIT_CRITICAL(&s_anim_lock);
}

void ws2812_set_audio_energy(uint8_t energy)
{
    taskENTER_CRITICAL(&s_anim_lock);
    s_audio_energy = energy;
    taskEXIT_CRITICAL(&s_anim_lock);
}

void ws2812_flash_wake(void)
{
    taskENTER_CRITICAL(&s_anim_lock);
    s_wake_flash_ms = 400;
    taskEXIT_CRITICAL(&s_anim_lock);
}

void ws2812_set_mode(led_mode_t mode)
{
    taskENTER_CRITICAL(&s_anim_lock);
    switch (mode) {
        case LED_MODE_IDLE:
            s_pattern_id = LED_PATTERN_COSMIC_NEBULA;
            break;
        case LED_MODE_WAKE:
            s_wake_flash_ms = 400;
            s_pattern_id = LED_PATTERN_CYBERPUNK_AURORA;
            break;
        case LED_MODE_STREAMING:
            s_pattern_id = LED_PATTERN_CYBERPUNK_AURORA;
            break;
        case LED_MODE_PTT:
            s_pattern_id = LED_PATTERN_PTT_ACTIVE;
            break;
        case LED_MODE_RNDIS:
            s_pattern_id = LED_PATTERN_RNDIS;
            break;
        case LED_MODE_DRIVE:
            s_pattern_id = LED_PATTERN_DRIVE_EMERALD;
            break;
        case LED_MODE_GIMBAL:
            s_pattern_id = LED_PATTERN_ORBITAL_AMBER;
            break;
        case LED_MODE_ERROR:
            s_pattern_id = LED_PATTERN_ERROR;
            break;
        default:
            break;
    }
    taskEXIT_CRITICAL(&s_anim_lock);
}
