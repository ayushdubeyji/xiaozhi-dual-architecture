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
    LED_MODE_DRIVE,        /*!< Physical Joystick Drive Mode: Emerald (0, 255, 80) */
    LED_MODE_GIMBAL,       /*!< Physical Pan/Tilt Gimbal Mode: Cyan/Violet (0, 200, 255) */
    LED_MODE_ERROR         /*!< Error state: Red (255, 0, 0) */
} led_mode_t;

typedef enum {
    CONTROL_MODE_DRIVE = 0,  /*!< Physical Joystick drives rover */
    CONTROL_MODE_GIMBAL = 1  /*!< Physical Joystick controls Pan/Tilt gimbal */
} control_mode_t;

/**
 * @brief Mecanum Rover ESP-NOW Standard Control Command Packet (12 bytes).
 */
typedef struct __attribute__((packed)) {
    uint8_t magic;         /*!< Always 0xA7 */
    uint8_t type;          /*!< 3=PKT_DRIVE, 4=PKT_STOP */
    int8_t  x;             /*!< Strafe: -100 (left) .. +100 (right) */
    int8_t  y;             /*!< Fwd/Rev: -100 (back) .. +100 (fwd) */
    int8_t  z;             /*!< Rotation: -100 (CCW) .. +100 (CW) */
    uint8_t speed;         /*!< Overall speed ceiling: 0 .. 100% */
    uint8_t mode;          /*!< Drive mode (1=Normal) */
    uint8_t flags;         /*!< Mode flags (0x01: E-stop) */
    int16_t arg;           /*!< Target heading or arg */
    uint16_t seq;          /*!< Rolling packet sequence counter */
} rover_pkt_t;

typedef rover_pkt_t rover_cmd_packet_t;

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
 * @brief Dispatch USB HID relative mouse wheel vertical scroll to host.
 *
 * @param scroll_step Positive = scroll up, Negative = scroll down.
 */
void usb_hid_send_mouse_scroll(int8_t scroll_step);

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

/**
 * @brief Initialize Hardware Controls (Analog Joystick ADC + Rotary Encoder).
 *
 * @return ESP_OK on success.
 */
esp_err_t hardware_controls_init(void);

/**
 * @brief Spawn background hardware control sampling and dispatch task.
 */
void hardware_controls_start_task(void);

/**
 * @brief Get current hardware control mode.
 */
control_mode_t hardware_controls_get_mode(void);

/**
 * @brief Get current Pan/Tilt gimbal pulse widths in microseconds (1000..2000).
 */
void hardware_controls_get_gimbal(int *pan_us, int *tilt_us);

#ifdef __cplusplus
}
#endif
