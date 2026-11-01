#include <stdio.h>
#include <string.h>
#include "esp_log.h"
#include "esp_system.h"
#include "nvs_flash.h"
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

// System Modules
#include "board_pins.h"
#include "audio_capture.h"
#include "wake_engine.h"
#include "usb_hid_trigger.h"

static const char *TAG = "s3_acoustic_node";

static void on_wake_event(wake_event_type_t event, const int16_t *clean_mono_pcm, size_t samples)
{
    switch (event) {
        case WAKE_EVENT_WAKENET:
            ESP_LOGI(TAG, "Event: WAKENET KEYWORD DETECTED");
            break;

        case WAKE_EVENT_PTT_DOWN:
            ESP_LOGI(TAG, "Event: HARDWARE PUSH-TO-TALK PRESSED");
            break;

        case WAKE_EVENT_PTT_UP:
            ESP_LOGI(TAG, "Event: HARDWARE PUSH-TO-TALK RELEASED");
            break;

        case WAKE_EVENT_VAD_SPEECH:
            // Continuous speech packet
            break;

        case WAKE_EVENT_VAD_SILENCE:
            ESP_LOGI(TAG, "Event: VAD SILENCE (Turn Completed)");
            break;

        default:
            break;
    }
}

void app_main(void)
{
    ESP_LOGI(TAG, "===============================================================");
    ESP_LOGI(TAG, " Waveshare ESP32-S3-Zero Distributed Xiaozhi Acoustic/HID Node");
    ESP_LOGI(TAG, " Dual-Core LX7 @ 240MHz | 4MB Flash | 2MB Quad PSRAM");
    ESP_LOGI(TAG, " Native USB: GPIO 19 (D-), GPIO 20 (D+) | WS2812: GPIO 21");
    ESP_LOGI(TAG, " I2S Dual MEMS: BCLK=4, WS=5, DIN=6 | PTT: GPIO 0, GPIO 1");
    ESP_LOGI(TAG, " Reserved SPI2 Display: DC=7, RST=8, BLK=9, CS=10, MOSI=11, SCK=12, MISO=13");
    ESP_LOGI(TAG, "===============================================================");

    // 1. Initialize Non-Volatile Storage (NVS)
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    // 2. Validate Memory Capacities (Internal SRAM + External PSRAM)
    size_t free_sram = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
    size_t free_psram = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
    ESP_LOGI(TAG, "Memory Status: Free Internal SRAM = %u KB, Free PSRAM = %u KB",
             (unsigned)(free_sram / 1024), (unsigned)(free_psram / 1024));

    // 3. Initialize Stereo I2S DMA Audio Capture (GPIO 4, 5, 6)
    ret = audio_capture_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "FATAL: Failed to init audio capture: %s", esp_err_to_name(ret));
        return;
    }

    // 4. Initialize TinyUSB Composite Device (HID Keyboard + CDC Stream + Net + WS2812)
    ret = usb_hid_trigger_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "FATAL: Failed to init USB subsystem: %s", esp_err_to_name(ret));
        return;
    }

    // 5. Initialize esp-sr Dual-Mic AFE & Wake Engine in PSRAM
    ret = wake_engine_init(on_wake_event);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "FATAL: Failed to init wake engine: %s", esp_err_to_name(ret));
        return;
    }

    // 6. Initialize Hardware Controls (Joystick ADC1: VRx=GP2, VRy=GP3, SW=GP1 | Encoder: A=GP14, B=GP15)
    ret = hardware_controls_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "FATAL: Failed to init hardware controls: %s", esp_err_to_name(ret));
        return;
    }
    hardware_controls_start_task();

    // 7. Spawn Core 0 & Core 1 Real-Time Tasks
    wake_engine_start_tasks();

    ESP_LOGI(TAG, "System running. Ready for WakeNet keyword, PTT trigger, CDC audio, and ESP-NOW rover commands.");
}
