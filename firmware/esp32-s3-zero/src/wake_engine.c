#include "wake_engine.h"
#include "audio_capture.h"
#include "board_pins.h"
#include "usb_hid_trigger.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"

// esp-sr headers
#include "esp_afe_sr_iface.h"
#include "esp_afe_sr_models.h"
#include "esp_afe_config.h"
#include "model_path.h"

static const char *TAG = "wake_engine";

static wake_engine_cb_t s_user_cb = NULL;
static esp_afe_sr_data_t *s_afe_data = NULL;
static const esp_afe_sr_iface_t *s_afe_handle = NULL;
static srmodel_list_t *s_models = NULL;

static TaskHandle_t s_feed_task_handle = NULL;
static TaskHandle_t s_fetch_task_handle = NULL;
static TaskHandle_t s_ptt_task_handle = NULL;

static volatile bool s_is_running = false;
static volatile bool s_ptt_pressed = false;
static volatile bool s_is_streaming = false;
static uint32_t s_silence_ticks = 0;
#define SILENCE_TIMEOUT_TICKS (pdMS_TO_TICKS(2000)) // 2 seconds hangover

static void ptt_monitor_task(void *pvParameters)
{
    ESP_LOGI(TAG, "Hardware PTT monitor started on GPIO 0 (BOOT) and GPIO 1 (EXT)");

    bool last_state = false; // false = released (HIGH), true = pressed (LOW)
    const TickType_t poll_period = pdMS_TO_TICKS(15);

    while (s_is_running) {
        // Active LOW inputs
        int boot_val = gpio_get_level((gpio_num_t)BOARD_PIN_PTT_BOOT);
        bool current_pressed = (boot_val == 0);

        if (current_pressed != last_state) {
            vTaskDelay(pdMS_TO_TICKS(20)); // Debounce
            boot_val = gpio_get_level((gpio_num_t)BOARD_PIN_PTT_BOOT);
            current_pressed = (boot_val == 0);

            if (current_pressed != last_state) {
                last_state = current_pressed;
                s_ptt_pressed = current_pressed;

                if (current_pressed) {
                    ESP_LOGI(TAG, ">>> PTT BUTTON PRESSED <<<");
                    s_is_streaming = true;
                    s_silence_ticks = 0;
                    usb_hid_send_ptt_keystroke(true);
                    ws2812_set_mode(LED_MODE_PTT);
                    if (s_user_cb) s_user_cb(WAKE_EVENT_PTT_DOWN, NULL, 0);
                } else {
                    ESP_LOGI(TAG, ">>> PTT BUTTON RELEASED <<<");
                    s_is_streaming = false;
                    usb_hid_send_ptt_keystroke(false);
                    ws2812_set_mode(LED_MODE_IDLE);
                    if (s_user_cb) s_user_cb(WAKE_EVENT_PTT_UP, NULL, 0);
                }
            }
        }

        vTaskDelay(poll_period);
    }

    vTaskDelete(NULL);
}

static void afe_feed_task(void *pvParameters)
{
    ESP_LOGI(TAG, "AFE Feed Task running on Core %d", xPortGetCoreID());

    int feed_chunksize = s_afe_handle->get_feed_chunksize(s_afe_data);
    int total_ch = s_afe_handle->get_feed_channel_num(s_afe_data);
    size_t samples_to_read = feed_chunksize * total_ch;
    size_t bytes_to_read = samples_to_read * sizeof(int16_t);

    int16_t *i2s_buff = (int16_t *)heap_caps_malloc(bytes_to_read, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    if (!i2s_buff) {
        ESP_LOGE(TAG, "Failed to allocate AFE feed buffer (%u bytes)", (unsigned)bytes_to_read);
        vTaskDelete(NULL);
        return;
    }

    while (s_is_running) {
        size_t bytes_read = 0;
        esp_err_t ret = audio_capture_read(i2s_buff, samples_to_read, &bytes_read, portMAX_DELAY);
        if (ret == ESP_OK && bytes_read == bytes_to_read) {
            s_afe_handle->feed(s_afe_data, i2s_buff);
        } else {
            vTaskDelay(pdMS_TO_TICKS(1));
        }
    }

    free(i2s_buff);
    vTaskDelete(NULL);
}

static void afe_fetch_task(void *pvParameters)
{
    ESP_LOGI(TAG, "AFE Fetch Task running on Core %d", xPortGetCoreID());

    while (s_is_running) {
        afe_fetch_result_t *res = s_afe_handle->fetch(s_afe_data);
        if (!res || res->ret_value == ESP_FAIL) {
            vTaskDelay(pdMS_TO_TICKS(5));
            continue;
        }

        // 1. WakeNet Keyword Detection
        if (res->wakeup_state == WAKENET_DETECTED) {
            ESP_LOGI(TAG, "*** WAKENET DETECTED! ***");
            s_is_streaming = true;
            s_silence_ticks = 0;

            usb_hid_send_wake_keystroke();
            ws2812_set_mode(LED_MODE_WAKE);

            if (s_user_cb) {
                s_user_cb(WAKE_EVENT_WAKENET, res->data, res->data_size / sizeof(int16_t));
            }
        }

        // 2. Audio Streaming & VAD State Handling
        if (s_is_streaming) {
            if (res->data && res->data_size > 0) {
                usb_stream_audio_frame((const int16_t *)res->data, res->data_size / sizeof(int16_t));
            }

            if (!s_ptt_pressed) {
                if (res->vad_state == VAD_SPEECH) {
                    s_silence_ticks = 0;
                    ws2812_set_mode(LED_MODE_STREAMING);
                } else {
                    s_silence_ticks += pdMS_TO_TICKS(10);
                    if (s_silence_ticks >= SILENCE_TIMEOUT_TICKS) {
                        ESP_LOGI(TAG, "VAD Silence timeout reached -> Returning to Idle");
                        s_is_streaming = false;
                        s_silence_ticks = 0;
                        ws2812_set_mode(LED_MODE_IDLE);
                        if (s_user_cb) s_user_cb(WAKE_EVENT_VAD_SILENCE, NULL, 0);
                    }
                }
            }
        }
    }

    vTaskDelete(NULL);
}

esp_err_t wake_engine_init(wake_engine_cb_t cb)
{
    s_user_cb = cb;

    ESP_LOGI(TAG, "Configuring esp-sr 2-mic AFE Pipeline with PSRAM Allocation");

    // Initialize Push-to-Talk GPIO (BOOT button)
    gpio_config_t ptt_cfg = {
        .pin_bit_mask = (1ULL << BOARD_PIN_PTT_BOOT),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&ptt_cfg);

    // esp-sr AFE Dual Mic Configuration
    s_models = esp_srmodel_init("model");
    afe_config_t *afe_config = afe_config_init("MM", s_models, AFE_TYPE_SR, AFE_MODE_HIGH_PERF);
    if (!afe_config) {
        ESP_LOGE(TAG, "Failed to allocate afe_config");
        return ESP_FAIL;
    }

    if (s_models != NULL) {
        afe_config->wakenet_init = true;
    } else {
        ESP_LOGW(TAG, "No wake models loaded from flash; WakeNet disabled, SE/VAD active");
        afe_config->wakenet_init = false;
    }
    afe_config->se_init = true;
    afe_config->vad_init = true;
    afe_config->memory_alloc_mode = AFE_MEMORY_ALLOC_MORE_PSRAM;

    s_afe_handle = esp_afe_handle_from_config(afe_config);
    if (!s_afe_handle) {
        ESP_LOGE(TAG, "Failed to obtain esp-sr AFE handle");
        afe_config_free(afe_config);
        return ESP_FAIL;
    }

    s_afe_data = s_afe_handle->create_from_config(afe_config);
    afe_config_free(afe_config);

    if (!s_afe_data) {
        ESP_LOGE(TAG, "Failed to create esp-sr AFE instance in PSRAM");
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "esp-sr AFE Pipeline initialized successfully");
    return ESP_OK;
}

esp_err_t wake_engine_start_tasks(void)
{
    if (s_is_running) return ESP_OK;
    s_is_running = true;

    // Start PTT monitor on Core 0
    xTaskCreatePinnedToCore(ptt_monitor_task, "ptt_mon", 3072, NULL, 5, &s_ptt_task_handle, 0);

    // Audio DSP and neural network wake-word tasks pinned to Core 1
    xTaskCreatePinnedToCore(afe_feed_task,  "afe_feed",  4096, NULL, 5, &s_feed_task_handle,  1);
    xTaskCreatePinnedToCore(afe_fetch_task, "afe_fetch", 6144, NULL, 6, &s_fetch_task_handle, 1);

    ESP_LOGI(TAG, "Wake Engine tasks spawned on Core 0 & Core 1");
    return ESP_OK;
}

bool wake_engine_is_ptt_pressed(void)
{
    return s_ptt_pressed;
}

void wake_engine_deinit(void)
{
    s_is_running = false;
    vTaskDelay(pdMS_TO_TICKS(100));

    if (s_afe_data && s_afe_handle) {
        s_afe_handle->destroy(s_afe_data);
        s_afe_data = NULL;
    }

    if (s_models) {
        esp_srmodel_deinit(s_models);
        s_models = NULL;
    }

    ESP_LOGI(TAG, "Wake Engine deinitialized");
}
