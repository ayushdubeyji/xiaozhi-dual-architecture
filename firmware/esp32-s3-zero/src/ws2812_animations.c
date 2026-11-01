#include <stdio.h>
#include "esp_log.h"
#include "esp_err.h"

#include "ws2812_animations.h"
#include "ringlight_driver.h"

static const char *TAG = "ws2812_compat";

esp_err_t ws2812_animations_init(void)
{
    ESP_LOGI(TAG, "Forwarding ws2812_animations_init to ringlight_driver_init");
    return ringlight_driver_init();
}

void ws2812_set_pattern(uint8_t pattern_id, int16_t hue_offset, uint16_t speed)
{
    ringlight_set_pattern(pattern_id, hue_offset, speed);
}

void ws2812_set_audio_energy(uint8_t energy)
{
    ringlight_set_audio_energy(energy);
}

void ws2812_flash_wake(void)
{
    ringlight_flash_wake();
}

void ws2812_set_mode(led_mode_t mode)
{
    ringlight_set_mode(mode);
}

void ws2812_set_brightness(uint8_t brightness_pct)
{
    ringlight_set_brightness(brightness_pct);
}
