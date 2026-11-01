#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    HAPTIC_NONE = 0,
    HAPTIC_TICK = 1,            /*!< Sharp exponential decay kick (8ms burst + smooth decay) */
    HAPTIC_PRESS = 2,           /*!< Ease-in-quad ramp (t^2, 25ms ramp + hold) */
    HAPTIC_RELEASE = 3,         /*!< Ease-out-cubic return */
    HAPTIC_DETENT = 4,          /*!< Gaussian bell-curve pulse (12ms) */
    HAPTIC_JOYSTICK_CENTER = 5, /*!< Quick 10ms click when joystick enters/leaves center deadzone */
    HAPTIC_ALERT = 6            /*!< Dual smooth sine-modulated pulse for E-Stop/warnings */
} haptic_effect_t;

/**
 * @brief Configure LEDC timer & channel on GPIO 16 at 15 kHz PWM and spawn 1ms easing engine task.
 * @return ESP_OK on success.
 */
esp_err_t haptics_engine_init(void);

/**
 * @brief Trigger a haptic feedback effect with mathematical easing curve.
 *
 * @param effect Target haptic effect (1-6).
 * @param intensity Base intensity (0-255). If 0, uses effect default.
 * @param duration_ms Total effect duration in milliseconds. If 0, uses effect default.
 */
void haptics_play(haptic_effect_t effect, uint8_t intensity, uint16_t duration_ms);

/**
 * @brief Stop active haptic feedback immediately and reset PWM to 0%.
 */
void haptics_stop(void);

#ifdef __cplusplus
}
#endif
