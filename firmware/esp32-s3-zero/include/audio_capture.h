#pragma once

#include <stdint.h>
#include <stddef.h>
#include "esp_err.h"
#include "freertos/FreeRTOS.h"

#ifdef __cplusplus
extern "C" {
#endif

#define AUDIO_CAPTURE_SAMPLE_RATE    16000
#define AUDIO_CAPTURE_CHANNELS       2
#define AUDIO_CAPTURE_BITS_PER_SAMPLE 16
#define AUDIO_CAPTURE_FRAME_SAMPLES  512

/**
 * @brief Initialize 2-channel I2S DMA capture on GPIO 4 (BCLK), 5 (WS), 6 (DIN).
 *
 * Configures stereo interleaved 16-bit capture at 16 kHz.
 *
 * @return ESP_OK on success, or appropriate error code.
 */
esp_err_t audio_capture_init(void);

/**
 * @brief Read stereo interleaved PCM samples from I2S DMA buffers.
 *
 * @param[out] dest Destination buffer for int16_t samples.
 * @param[in] samples_to_read Number of int16_t samples to read (frames * channels).
 * @param[out] bytes_read Number of bytes successfully copied.
 * @param[in] timeout_ticks Maximum FreeRTOS ticks to wait for DMA data.
 * @return ESP_OK on success.
 */
esp_err_t audio_capture_read(int16_t *dest, size_t samples_to_read, size_t *bytes_read, TickType_t timeout_ticks);

/**
 * @brief Stop and deinitialize the I2S capture peripheral.
 */
void audio_capture_deinit(void);

#ifdef __cplusplus
}
#endif
