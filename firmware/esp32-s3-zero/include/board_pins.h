#pragma once

#include <stdint.h>

/**
 * @file board_pins.h
 * @brief Waveshare ESP32-S3-Zero Board Pinout & Compile-Time Static Assertions.
 *
 * Dedicated pin mapping for the distributed Xiaozhi acoustic & HID trigger node:
 *  - USB Native D-/D+: GP19, GP20
 *  - Onboard Telemetry LED: GP21 (WS2812 RMT)
 *  - Dual I2S MEMS Mics: GP4 (BCLK), GP5 (WS), GP6 (DIN)
 *  - Shared PTT/Click Switch: GP1 (Joystick SW + Mouse Wheel Click wired in parallel)
 *  - Analog Joystick ADC1: GP2 (VRx / CH1), GP3 (VRy / CH2)
 *  - 56mm Hollow Shaft Encoder (Camera Focus Ring): GP7 (Phase A), GP9 (Phase B), COM GND (No Click Switch)
 *  - Haptic Motor: GP8 (15 kHz LEDC PWM)
 *  - 16-LED Ringlight: GP10 (DI, powered from 5V pin 1)
 *  - Free Spare: GP11 (Right header spare)
 *  - Mouse Scroll Wheel: GP12 (Phase A), GP13 (Phase B), COM GND
 */

#ifdef __cplusplus
extern "C" {
#endif

/* =========================================================================
 * 1. NATIVE HARDWARE USB (ESP32-S3 Internal PHY)
 * ========================================================================= */
#define BOARD_PIN_USB_DM        19  /*!< Native USB D- (GPIO 19) */
#define BOARD_PIN_USB_DP        20  /*!< Native USB D+ (GPIO 20) */

/* =========================================================================
 * 2. ONBOARD TELEMETRY & 16-LED RINGLIGHT
 * ========================================================================= */
#define BOARD_PIN_WS2812        21  /*!< Waveshare S3-Zero Onboard RGB LED (RMT backend) */
#define BOARD_PIN_RINGLIGHT     10  /*!< 16-LED Ringlight Data In (Powered from 5V pin 1) */
#define BOARD_RINGLIGHT_LEDS    16  /*!< 16 addressable RGB LEDs */

/* =========================================================================
 * 3. DUAL I2S MEMS MICROPHONES (INMP441 / ICS-43434)
 *    Left Mic  L/R wired to GND  -> Captured on Left Channel
 *    Right Mic L/R wired to 3.3V -> Captured on Right Channel
 * ========================================================================= */
#define BOARD_PIN_I2S_BCLK      4   /*!< I2S Bit Clock (Continuous) */
#define BOARD_PIN_I2S_WS        5   /*!< I2S Word Select / LRCLK (16 kHz) */
#define BOARD_PIN_I2S_DIN       6   /*!< I2S Serial Data In */

/* =========================================================================
 * 4. SHARED HARDWARE PTT & CLICK TRIGGER SWITCH
 *    Both Joystick Click SW and Mouse Wheel Click SW soldered in parallel to GP1.
 * ========================================================================= */
#define BOARD_PIN_PTT_BOOT      0   /*!< Onboard BOOT Button (Active LOW, internal pull-up) */
#define BOARD_PIN_PTT_EXT       1   /*!< Shared Header Pin GP1 (Active LOW, internal pull-up) */
#define BOARD_PIN_SHARED_SW     BOARD_PIN_PTT_EXT /*!< Shared Joystick SW + Mouse Wheel Click */
#define BOARD_PIN_JOY_SW        BOARD_PIN_PTT_EXT /*!< Joystick Click Switch (GP1) */
#define BOARD_PIN_MOUSE_SW      BOARD_PIN_PTT_EXT /*!< Mouse Wheel Click Switch (GP1) */

/* =========================================================================
 * 5. 56mm HOLLOW SHAFT ROTARY ENCODER (CAMERA FOCUS RING - NO CLICK SWITCH)
 *    Strictly 2-phase continuous rotary ring encircling Arducam IMX519 lens.
 * ========================================================================= */
#define BOARD_PIN_HOLLOW_ENC_A  7   /*!< 56mm Hollow Ring Phase A (GPIO 7, Pull-Up) */
#define BOARD_PIN_HOLLOW_ENC_B  9   /*!< 56mm Hollow Ring Phase B (GPIO 9, Pull-Up) */

/* =========================================================================
 * 6. MOUSE SCROLL WHEEL (2-PHASE ROTARY WHEEL)
 * ========================================================================= */
#define BOARD_PIN_MOUSE_ENC_A   12  /*!< Mouse Scroll Wheel Phase A (GPIO 12, Pull-Up) */
#define BOARD_PIN_MOUSE_ENC_B   13  /*!< Mouse Scroll Wheel Phase B (GPIO 13, Pull-Up) */

/* Legacy aliases for backward compatibility */
#define BOARD_PIN_ENC_A         BOARD_PIN_MOUSE_ENC_A
#define BOARD_PIN_ENC_B         BOARD_PIN_MOUSE_ENC_B

/* =========================================================================
 * 7. HARDWARE JOYSTICK ADC (WI-FI SAFE ADC1)
 * ========================================================================= */
#define BOARD_PIN_JOY_VRX       2   /*!< Analog Joystick VRx (ADC1_CH1) */
#define BOARD_PIN_JOY_VRY       3   /*!< Analog Joystick VRy (ADC1_CH2) */

/* =========================================================================
 * 8. HAPTIC FEEDBACK MOTOR (15 kHz LEDC PWM)
 * ========================================================================= */
#define BOARD_PIN_HAPTIC_PWM    8   /*!< Haptic feedback motor PWM drive (GPIO 8) */

/* =========================================================================
 * 9. FREE SPARE PIN
 * ========================================================================= */
#define BOARD_PIN_SPARE         11  /*!< Free spare on right-side header (GPIO 11) */

/* =========================================================================
 * 10. COMPILE-TIME PIN COLLISION STATIC ASSERTIONS
 * ========================================================================= */

/* USB PHY Pin Sanity */
_Static_assert(BOARD_PIN_USB_DM != BOARD_PIN_USB_DP, "USB D- and D+ pins must be distinct");

/* LED Isolation */
_Static_assert(BOARD_PIN_WS2812 != BOARD_PIN_USB_DM && BOARD_PIN_WS2812 != BOARD_PIN_USB_DP, "WS2812 collision with USB");
_Static_assert(BOARD_PIN_RINGLIGHT != BOARD_PIN_USB_DM && BOARD_PIN_RINGLIGHT != BOARD_PIN_USB_DP, "Ringlight collision with USB");
_Static_assert(BOARD_PIN_RINGLIGHT != BOARD_PIN_WS2812, "Ringlight collision with onboard WS2812");

/* I2S Bus Internal Uniqueness */
_Static_assert(BOARD_PIN_I2S_BCLK != BOARD_PIN_I2S_WS,  "I2S BCLK and WS collide");
_Static_assert(BOARD_PIN_I2S_BCLK != BOARD_PIN_I2S_DIN, "I2S BCLK and DIN collide");
_Static_assert(BOARD_PIN_I2S_WS   != BOARD_PIN_I2S_DIN, "I2S WS and DIN collide");

/* I2S vs USB & LEDs */
_Static_assert(BOARD_PIN_I2S_BCLK != BOARD_PIN_WS2812 && BOARD_PIN_I2S_WS != BOARD_PIN_WS2812 && BOARD_PIN_I2S_DIN != BOARD_PIN_WS2812, "I2S collides with WS2812");
_Static_assert(BOARD_PIN_I2S_BCLK != BOARD_PIN_RINGLIGHT && BOARD_PIN_I2S_WS != BOARD_PIN_RINGLIGHT && BOARD_PIN_I2S_DIN != BOARD_PIN_RINGLIGHT, "I2S collides with Ringlight");
_Static_assert(BOARD_PIN_I2S_BCLK != BOARD_PIN_USB_DM && BOARD_PIN_I2S_BCLK != BOARD_PIN_USB_DP, "I2S BCLK collides with USB");
_Static_assert(BOARD_PIN_I2S_WS   != BOARD_PIN_USB_DM && BOARD_PIN_I2S_WS   != BOARD_PIN_USB_DP, "I2S WS collides with USB");
_Static_assert(BOARD_PIN_I2S_DIN  != BOARD_PIN_USB_DM && BOARD_PIN_I2S_DIN  != BOARD_PIN_USB_DP, "I2S DIN collides with USB");

/* PTT Button Isolation */
_Static_assert(BOARD_PIN_PTT_BOOT != BOARD_PIN_SHARED_SW, "PTT BOOT and SHARED_SW collide");
_Static_assert(BOARD_PIN_PTT_BOOT != BOARD_PIN_I2S_BCLK && BOARD_PIN_PTT_BOOT != BOARD_PIN_I2S_WS && BOARD_PIN_PTT_BOOT != BOARD_PIN_I2S_DIN, "PTT BOOT collides with I2S");
_Static_assert(BOARD_PIN_SHARED_SW != BOARD_PIN_I2S_BCLK && BOARD_PIN_SHARED_SW != BOARD_PIN_I2S_WS && BOARD_PIN_SHARED_SW != BOARD_PIN_I2S_DIN, "PTT SHARED_SW collides with I2S");
_Static_assert(BOARD_PIN_SHARED_SW != BOARD_PIN_WS2812 && BOARD_PIN_SHARED_SW != BOARD_PIN_RINGLIGHT, "PTT SHARED_SW collides with LEDs");

/* Joystick ADC Isolation */
_Static_assert(BOARD_PIN_JOY_VRX != BOARD_PIN_JOY_VRY, "Joystick VRx and VRy collide");
_Static_assert(BOARD_PIN_JOY_VRX != BOARD_PIN_SHARED_SW && BOARD_PIN_JOY_VRY != BOARD_PIN_SHARED_SW, "Joystick analog collides with SW");
_Static_assert(BOARD_PIN_JOY_VRX != BOARD_PIN_I2S_BCLK && BOARD_PIN_JOY_VRX != BOARD_PIN_I2S_WS && BOARD_PIN_JOY_VRX != BOARD_PIN_I2S_DIN, "Joystick VRx collides with I2S");
_Static_assert(BOARD_PIN_JOY_VRY != BOARD_PIN_I2S_BCLK && BOARD_PIN_JOY_VRY != BOARD_PIN_I2S_WS && BOARD_PIN_JOY_VRY != BOARD_PIN_I2S_DIN, "Joystick VRy collides with I2S");

/* 56mm Hollow Shaft Encoder Uniqueness & Isolation */
_Static_assert(BOARD_PIN_HOLLOW_ENC_A != BOARD_PIN_HOLLOW_ENC_B, "Hollow Encoder A and B collide");
_Static_assert(BOARD_PIN_HOLLOW_ENC_A != BOARD_PIN_SHARED_SW && BOARD_PIN_HOLLOW_ENC_B != BOARD_PIN_SHARED_SW, "Hollow Encoder collides with Shared SW");
_Static_assert(BOARD_PIN_HOLLOW_ENC_A != BOARD_PIN_JOY_VRX   && BOARD_PIN_HOLLOW_ENC_B != BOARD_PIN_JOY_VRX,   "Hollow Encoder collides with Joystick VRx");
_Static_assert(BOARD_PIN_HOLLOW_ENC_A != BOARD_PIN_JOY_VRY   && BOARD_PIN_HOLLOW_ENC_B != BOARD_PIN_JOY_VRY,   "Hollow Encoder collides with Joystick VRy");
_Static_assert(BOARD_PIN_HOLLOW_ENC_A != BOARD_PIN_I2S_BCLK  && BOARD_PIN_HOLLOW_ENC_B != BOARD_PIN_I2S_BCLK,  "Hollow Encoder collides with I2S BCLK");
_Static_assert(BOARD_PIN_HOLLOW_ENC_A != BOARD_PIN_I2S_WS    && BOARD_PIN_HOLLOW_ENC_B != BOARD_PIN_I2S_WS,    "Hollow Encoder collides with I2S WS");
_Static_assert(BOARD_PIN_HOLLOW_ENC_A != BOARD_PIN_I2S_DIN   && BOARD_PIN_HOLLOW_ENC_B != BOARD_PIN_I2S_DIN,   "Hollow Encoder collides with I2S DIN");

/* Mouse Scroll Wheel Uniqueness & Isolation */
_Static_assert(BOARD_PIN_MOUSE_ENC_A != BOARD_PIN_MOUSE_ENC_B, "Mouse Scroll A and B collide");
_Static_assert(BOARD_PIN_MOUSE_ENC_A != BOARD_PIN_HOLLOW_ENC_A && BOARD_PIN_MOUSE_ENC_A != BOARD_PIN_HOLLOW_ENC_B, "Mouse Scroll A collides with Hollow Encoder");
_Static_assert(BOARD_PIN_MOUSE_ENC_B != BOARD_PIN_HOLLOW_ENC_A && BOARD_PIN_MOUSE_ENC_B != BOARD_PIN_HOLLOW_ENC_B, "Mouse Scroll B collides with Hollow Encoder");
_Static_assert(BOARD_PIN_MOUSE_ENC_A != BOARD_PIN_SHARED_SW    && BOARD_PIN_MOUSE_ENC_B != BOARD_PIN_SHARED_SW,    "Mouse Scroll collides with Shared SW");

/* Haptic Motor (GPIO 8) Isolation */
_Static_assert(BOARD_PIN_HAPTIC_PWM != BOARD_PIN_USB_DM && BOARD_PIN_HAPTIC_PWM != BOARD_PIN_USB_DP, "Haptic PWM collides with USB");
_Static_assert(BOARD_PIN_HAPTIC_PWM != BOARD_PIN_WS2812 && BOARD_PIN_HAPTIC_PWM != BOARD_PIN_RINGLIGHT, "Haptic PWM collides with LEDs");
_Static_assert(BOARD_PIN_HAPTIC_PWM != BOARD_PIN_I2S_BCLK && BOARD_PIN_HAPTIC_PWM != BOARD_PIN_I2S_WS && BOARD_PIN_HAPTIC_PWM != BOARD_PIN_I2S_DIN, "Haptic PWM collides with I2S");
_Static_assert(BOARD_PIN_HAPTIC_PWM != BOARD_PIN_PTT_BOOT && BOARD_PIN_HAPTIC_PWM != BOARD_PIN_SHARED_SW, "Haptic PWM collides with PTT/SW");
_Static_assert(BOARD_PIN_HAPTIC_PWM != BOARD_PIN_JOY_VRX && BOARD_PIN_HAPTIC_PWM != BOARD_PIN_JOY_VRY, "Haptic PWM collides with Joystick");
_Static_assert(BOARD_PIN_HAPTIC_PWM != BOARD_PIN_HOLLOW_ENC_A && BOARD_PIN_HAPTIC_PWM != BOARD_PIN_HOLLOW_ENC_B, "Haptic PWM collides with Hollow Encoder");
_Static_assert(BOARD_PIN_HAPTIC_PWM != BOARD_PIN_MOUSE_ENC_A && BOARD_PIN_HAPTIC_PWM != BOARD_PIN_MOUSE_ENC_B, "Haptic PWM collides with Mouse Encoder");

/* Ringlight (GPIO 10) Isolation */
_Static_assert(BOARD_PIN_RINGLIGHT != BOARD_PIN_HAPTIC_PWM, "Ringlight collides with Haptic PWM");
_Static_assert(BOARD_PIN_RINGLIGHT != BOARD_PIN_HOLLOW_ENC_A && BOARD_PIN_RINGLIGHT != BOARD_PIN_HOLLOW_ENC_B, "Ringlight collides with Hollow Encoder");
_Static_assert(BOARD_PIN_RINGLIGHT != BOARD_PIN_MOUSE_ENC_A && BOARD_PIN_RINGLIGHT != BOARD_PIN_MOUSE_ENC_B, "Ringlight collides with Mouse Encoder");
_Static_assert(BOARD_PIN_RINGLIGHT != BOARD_PIN_JOY_VRX && BOARD_PIN_RINGLIGHT != BOARD_PIN_JOY_VRY, "Ringlight collides with Joystick");

/* Free Spare (GPIO 11) Isolation */
_Static_assert(BOARD_PIN_SPARE != BOARD_PIN_RINGLIGHT && BOARD_PIN_SPARE != BOARD_PIN_HAPTIC_PWM, "Spare pin collides with Ringlight/Haptic");
_Static_assert(BOARD_PIN_SPARE != BOARD_PIN_HOLLOW_ENC_A && BOARD_PIN_SPARE != BOARD_PIN_HOLLOW_ENC_B, "Spare pin collides with Hollow Encoder");
_Static_assert(BOARD_PIN_SPARE != BOARD_PIN_MOUSE_ENC_A && BOARD_PIN_SPARE != BOARD_PIN_MOUSE_ENC_B, "Spare pin collides with Mouse Encoder");

#ifdef __cplusplus
}
#endif
