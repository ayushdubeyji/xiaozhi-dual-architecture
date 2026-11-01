#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"
#include "usb_hid_trigger.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    LED_PATTERN_COSMIC_NEBULA = 0,   /*!< Idle: Slow breathing chromatic drift (purple, cosmic blue, soft emerald) */
    LED_PATTERN_CYBERPUNK_AURORA = 1,/*!< Xiaozhi Agent: Dynamic audio-reactive wave / breathing aurora (cyan, violet, aqua) */
    LED_PATTERN_ORBITAL_AMBER = 2,   /*!< Gimbal Mode: Warm amber orbital pulse */
    LED_PATTERN_QUANTUM_WAVE = 3,    /*!< Quantum Wave: Fast cyan/magenta interference wave */
    LED_PATTERN_SOLAR_FLARE = 4,     /*!< Solar Flare: Crimson to solar gold dynamic flare */
    LED_PATTERN_DRIVE_EMERALD = 5,   /*!< Rover Drive Mode: Lush emerald wave */
    LED_PATTERN_PTT_ACTIVE = 6,      /*!< Hardware PTT: Golden yellow intense glow */
    LED_PATTERN_RNDIS = 7,           /*!< Emergency RNDIS: Deep electric violet pulse */
    LED_PATTERN_ERROR = 8            /*!< System Error: Urgent red pulse */
} led_pattern_id_t;

/**
 * @brief Initialize WS2812 RMT strip on GPIO 21 and launch rendering task.
 * @return ESP_OK on success.
 */
esp_err_t ws2812_animations_init(void);

/**
 * @brief Set the active pattern, hue offset, and speed.
 *
 * @param pattern_id Pattern index (0..8).
 * @param hue_offset Fine-tuning hue rotation (-180..360).
 * @param speed Speed percentage (e.g. 100 = 1.0x, 200 = 2.0x, 50 = 0.5x).
 */
void ws2812_set_pattern(uint8_t pattern_id, int16_t hue_offset, uint16_t speed);

/**
 * @brief Inject audio energy (0-255) for dynamic audio-reactive modulation.
 */
void ws2812_set_audio_energy(uint8_t energy);

/**
 * @brief Temporary wake flash effect.
 */
void ws2812_flash_wake(void);

/**
 * @brief Set overall brightness percentage (0..100).
 */
void ws2812_set_brightness(uint8_t brightness_pct);

#ifdef __cplusplus
}
#endif
