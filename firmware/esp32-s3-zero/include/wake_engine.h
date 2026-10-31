#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    WAKE_EVENT_NONE = 0,
    WAKE_EVENT_WAKENET,     /*!< WakeNet keyword detected via 2-mic beamforming */
    WAKE_EVENT_PTT_DOWN,    /*!< Push-to-talk button pressed (GPIO 0 or GPIO 1) */
    WAKE_EVENT_PTT_UP,      /*!< Push-to-talk button released */
    WAKE_EVENT_VAD_SPEECH,  /*!< Active human speech detected in audio stream */
    WAKE_EVENT_VAD_SILENCE  /*!< Speech ended / silence timeout reached */
} wake_event_type_t;

/**
 * @brief Callback signature for wake engine events and clean 16kHz mono audio output.
 *
 * @param event Type of wake/PTT/VAD event.
 * @param clean_mono_pcm Pointer to clean post-AFE mono 16-bit 16kHz audio samples (or NULL).
 * @param samples Number of int16_t samples available in clean_mono_pcm.
 */
typedef void (*wake_engine_cb_t)(wake_event_type_t event, const int16_t *clean_mono_pcm, size_t samples);

/**
 * @brief Initialize the esp-sr 2-mic AFE pipeline in PSRAM and PTT GPIO interrupts.
 *
 * @param cb Callback function to receive wake events and processed audio.
 * @return ESP_OK on success.
 */
esp_err_t wake_engine_init(wake_engine_cb_t cb);

/**
 * @brief Start the FreeRTOS audio feed and fetch tasks on Core 1.
 *
 * @return ESP_OK on success.
 */
esp_err_t wake_engine_start_tasks(void);

/**
 * @brief Check if either PTT button (GPIO 0 or GPIO 1) is currently depressed.
 *
 * @return true if pressed, false otherwise.
 */
bool wake_engine_is_ptt_pressed(void);

/**
 * @brief Stop and clean up the wake engine.
 */
void wake_engine_deinit(void);

#ifdef __cplusplus
}
#endif
