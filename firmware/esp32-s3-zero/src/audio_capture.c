#include "audio_capture.h"
#include "board_pins.h"
#include "esp_log.h"
#include "esp_idf_version.h"
#include "driver/i2s_std.h"

static const char *TAG = "audio_capture";

static i2s_chan_handle_t s_rx_chan = NULL;
static bool s_is_initialized = false;
static int32_t s_raw_dma_buf[1024];

esp_err_t audio_capture_init(void)
{
    if (s_is_initialized) {
        ESP_LOGW(TAG, "Audio capture already initialized");
        return ESP_OK;
    }

    ESP_LOGI(TAG, "Initializing Stereo I2S Capture: BCLK=%d, WS=%d, DIN=%d @ %dHz",
             BOARD_PIN_I2S_BCLK, BOARD_PIN_I2S_WS, BOARD_PIN_I2S_DIN, AUDIO_CAPTURE_SAMPLE_RATE);

    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
    chan_cfg.dma_desc_num = 6;
    chan_cfg.dma_frame_num = AUDIO_CAPTURE_FRAME_SAMPLES;
    chan_cfg.auto_clear = true;

    esp_err_t ret = i2s_new_channel(&chan_cfg, NULL, &s_rx_chan);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to allocate I2S RX channel: %s", esp_err_to_name(ret));
        return ret;
    }

    // Configure 32-bit slot width for INMP441 / ICS-43434 MEMS mics (64 SCK cycles/frame)
    i2s_std_config_t std_cfg = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(AUDIO_CAPTURE_SAMPLE_RATE),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_32BIT, I2S_SLOT_MODE_STEREO),
        .gpio_cfg = {
            .mclk = I2S_GPIO_UNUSED,
            .bclk = (gpio_num_t)BOARD_PIN_I2S_BCLK,
            .ws   = (gpio_num_t)BOARD_PIN_I2S_WS,
            .dout = I2S_GPIO_UNUSED,
            .din  = (gpio_num_t)BOARD_PIN_I2S_DIN,
            .invert_flags = {
                .mclk_inv = false,
                .bclk_inv = false,
                .ws_inv   = false,
            },
        },
    };
    std_cfg.slot_cfg.slot_bit_width = I2S_SLOT_BIT_WIDTH_32BIT;

    ret = i2s_channel_init_std_mode(s_rx_chan, &std_cfg);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize I2S std mode: %s", esp_err_to_name(ret));
        i2s_del_channel(s_rx_chan);
        s_rx_chan = NULL;
        return ret;
    }

    ret = i2s_channel_enable(s_rx_chan);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to enable I2S RX channel: %s", esp_err_to_name(ret));
        i2s_del_channel(s_rx_chan);
        s_rx_chan = NULL;
        return ret;
    }

    s_is_initialized = true;
    ESP_LOGI(TAG, "Stereo I2S capture active (32-bit slot, 16kHz, DMA enabled)");
    return ESP_OK;
}

esp_err_t audio_capture_read(int16_t *dest, size_t samples_to_read, size_t *bytes_read, TickType_t timeout_ticks)
{
    if (!s_is_initialized || dest == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    size_t samples_batch = (samples_to_read > 1024) ? 1024 : samples_to_read;
    size_t req_bytes = samples_batch * sizeof(int32_t);
    size_t actual_dma_bytes = 0;

    esp_err_t ret = i2s_channel_read(s_rx_chan, s_raw_dma_buf, req_bytes, &actual_dma_bytes, timeout_ticks);
    if (ret == ESP_OK) {
        size_t samples_read = actual_dma_bytes / sizeof(int32_t);
        for (size_t i = 0; i < samples_read; i++) {
            // INMP441/ICS-43434 24-bit aligned: shift >> 14 to convert to standard 16-bit PCM
            dest[i] = (int16_t)(s_raw_dma_buf[i] >> 14);
        }
        if (bytes_read) {
            *bytes_read = samples_read * sizeof(int16_t);
        }
    }
    return ret;
}

void audio_capture_deinit(void)
{
    if (!s_is_initialized) return;

    if (s_rx_chan) {
        i2s_channel_disable(s_rx_chan);
        i2s_del_channel(s_rx_chan);
        s_rx_chan = NULL;
    }

    s_is_initialized = false;
    ESP_LOGI(TAG, "Audio capture stopped");
}
