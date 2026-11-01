#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"
#include "board_pins.h"
#include "usb_hid_trigger.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    RINGLIGHT_PATTERN_COSMIC_NEBULA = 0,   /*!< Idle: Slow breathing chromatic drift around 16 LEDs */
    RINGLIGHT_PATTERN_CYBERPUNK_AURORA = 1,/*!< Dynamic audio-reactive wave / rotating aurora */
    RINGLIGHT_PATTERN_ORBITAL_AMBER = 2,   /*!< Gimbal Mode: Warm amber orbital pulse / comet chase */
    RINGLIGHT_PATTERN_QUANTUM_WAVE = 3,    /*!< Quantum Wave: Fast interference wave */
    RINGLIGHT_PATTERN_SOLAR_FLARE = 4,     /*!< Solar Flare: Crimson to solar gold dynamic flare */
    RINGLIGHT_PATTERN_DRIVE_EMERALD = 5,   /*!< Rover Drive Mode: Lush emerald wave */
    RINGLIGHT_PATTERN_PTT_ACTIVE = 6,      /*!< Hardware PTT: Golden yellow intense glow */
    RINGLIGHT_PATTERN_RNDIS = 7,           /*!< Emergency RNDIS: Deep electric violet pulse */
    RINGLIGHT_PATTERN_ERROR = 8            /*!< System Error: Urgent red pulse */
} ringlight_pattern_id_t;

/**
 * @brief Initialize 16-LED Ringlight on GP10 (and onboard WS2812 on GP21) via RMT backend.
 * @return ESP_OK on success.
 */
esp_err_t ringlight_driver_init(void);

/**
 * @brief Set the active pattern, hue offset, and speed for 16-LED Ringlight.
 *
 * @param pattern_id Pattern index (0..8).
 * @param hue_offset Fine-tuning hue rotation (-180..360).
 * @param speed Speed percentage (e.g. 100 = 1.0x, 200 = 2.0x, 50 = 0.5x).
 */
void ringlight_set_pattern(uint8_t pattern_id, int16_t hue_offset, uint16_t speed);

/**
 * @brief Set overall brightness percentage (0..100).
 */
void ringlight_set_brightness(uint8_t brightness_pct);

/**
 * @brief Inject audio energy (0-255) for dynamic audio-reactive modulation.
 */
void ringlight_set_audio_energy(uint8_t energy);

/**
 * @brief Temporary wake flash effect across the ringlight.
 */
void ringlight_flash_wake(void);

/**
 * @brief Update ringlight visual mode (maps to system state).
 */
void ringlight_set_mode(led_mode_t mode);

#ifdef __cplusplus
}
#endif
