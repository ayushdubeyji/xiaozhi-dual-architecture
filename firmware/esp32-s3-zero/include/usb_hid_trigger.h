#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define ROVER_PACKET_MAGIC 0xA7

typedef enum {
    LED_MODE_IDLE = 0,     /*!< Idle state: Dim Blue (0, 0, 16) */
    LED_MODE_WAKE,         /*!< WakeNet triggered: Cyan Flash (0, 255, 255) */
    LED_MODE_STREAMING,    /*!< Audio streaming to Xiaozhi: Pulsing Green (0, 255, 0) */
    LED_MODE_PTT,          /*!< Hardware PTT Active: Amber / Yellow (255, 140, 0) */
    LED_MODE_RNDIS,        /*!< Emergency RNDIS Active: Purple (140, 0, 255) */
    LED_MODE_ERROR         /*!< Error state: Red (255, 0, 0) */
} led_mode_t;

/**
 * @brief Mecanum Rover ESP-NOW Control Command Packet.
 */
typedef struct __attribute__((packed)) {
    uint8_t magic;         /*!< Always 0xA7 */
    uint8_t seq;           /*!< Rolling packet sequence counter */
    int8_t  vx;            /*!< Longitudinal velocity (-100 to 100) */
    int8_t  vy;            /*!< Lateral strafe velocity (-100 to 100) */
    int8_t  omega;         /*!< Rotational yaw rate (-100 to 100) */
    uint8_t buttons;       /*!< Bit0: Horn, Bit1: Lights, Bit2: Arm, Bit3: E-Stop */
    uint8_t flags;         /*!< Mode flags (0x01: Teleop, 0x02: Auto-dock) */
    uint8_t crc;           /*!< XOR checksum */
} rover_cmd_packet_t;

/**
 * @brief Mecanum Rover ESP-NOW Telemetry Packet.
 */
typedef struct __attribute__((packed)) {
    uint8_t magic;         /*!< Always 0xA7 */
    uint8_t seq;           /*!< Packet sequence counter */
    uint8_t battery_pct;   /*!< Battery state of charge (0-100%) */
    int16_t tof_front_mm;  /*!< Front Time-of-Flight range in mm */
    int16_t tof_rear_mm;   /*!< Rear Time-of-Flight range in mm */
    uint8_t status_flags;  /*!< Bit0: Moving, Bit1: Obstacle, Bit2: Low Bat */
    uint8_t crc;           /*!< XOR checksum */
} rover_telem_packet_t;

/**
 * @brief Initialize TinyUSB composite device (HID + CDC + Net) and WS2812 RMT driver.
 *
 * @return ESP_OK on success.
 */
esp_err_t usb_hid_trigger_init(void);

/**
 * @brief Dispatch Ctrl+Shift+Alt+W HID keyboard report to Raspberry Pi host.
 */
void usb_hid_send_wake_keystroke(void);

/**
 * @brief Dispatch Ctrl+Shift+Alt+P press/release HID keyboard report to host.
 *
 * @param pressed true for keydown, false for keyup.
 */
void usb_hid_send_ptt_keystroke(bool pressed);

/**
 * @brief Stream clean 16kHz mono PCM audio frames over USB CDC ACM to Pi host.
 *
 * @param pcm_mono Pointer to 16-bit mono audio samples.
 * @param samples Number of samples in buffer.
 */
void usb_stream_audio_frame(const int16_t *pcm_mono, size_t samples);

/**
 * @brief Update WS2812 onboard LED visual feedback state.
 *
 * @param mode Target visual telemetry mode.
 */
void ws2812_set_mode(led_mode_t mode);

/**
 * @brief Activate on-demand RNDIS Wi-Fi NAT failover (Connects to SSID "Ayush").
 *
 * @return ESP_OK on initiation.
 */
esp_err_t rndis_failover_start(void);

/**
 * @brief Deactivate RNDIS Wi-Fi failover and return Wi-Fi to dedicated ESP-NOW mode.
 *
 * @return ESP_OK on success.
 */
esp_err_t rndis_failover_stop(void);

/**
 * @brief Query whether RNDIS Ethernet failover is currently running.
 *
 * @return true if active, false if idle/off.
 */
bool rndis_is_active(void);

/**
 * @brief Initialize ESP-NOW Mecanum Rover bridge on the current 2.4GHz channel.
 *
 * @return ESP_OK on success.
 */
esp_err_t espnow_rover_init(void);

/**
 * @brief Dispatch Mecanum Rover control packet over ESP-NOW.
 *
 * @param vx Forward/backward velocity (-100 to 100).
 * @param vy Strafe velocity (-100 to 100).
 * @param omega Rotational yaw velocity (-100 to 100).
 * @param buttons Hardware button bitfield.
 * @param flags Operating mode flags.
 * @return ESP_OK on transmit.
 */
esp_err_t espnow_rover_send_command(int8_t vx, int8_t vy, int8_t omega, uint8_t buttons, uint8_t flags);

#ifdef __cplusplus
}
#endif
