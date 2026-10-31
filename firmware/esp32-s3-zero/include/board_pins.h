#pragma once

#include <stdint.h>

/**
 * @file board_pins.h
 * @brief Waveshare ESP32-S3-Zero Board Pinout & Compile-Time Static Assertions.
 *
 * Dedicated pin mapping for the distributed Xiaozhi acoustic & HID trigger node.
 * Strictly isolates I2S digital audio, USB D+/D-, WS2812 status LED, PTT buttons,
 * and pre-reserves an entire SPI2 peripheral bus for future 1.3-1.69" displays
 * with zero hardware rewiring.
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
 * 2. ONBOARD TELEMETRY (WS2812 RGB LED)
 * ========================================================================= */
#define BOARD_PIN_WS2812        21  /*!< Waveshare S3-Zero Onboard RGB LED (RMT backend) */

/* =========================================================================
 * 3. DUAL I2S MEMS MICROPHONES (INMP441 / ICS-43434)
 *    Left Mic  L/R wired to GND  -> Captured on Left Channel
 *    Right Mic L/R wired to 3.3V -> Captured on Right Channel
 * ========================================================================= */
#define BOARD_PIN_I2S_BCLK      4   /*!< I2S Bit Clock (Continuous) */
#define BOARD_PIN_I2S_WS        5   /*!< I2S Word Select / LRCLK (16 kHz) */
#define BOARD_PIN_I2S_DIN       6   /*!< I2S Serial Data In */

/* =========================================================================
 * 4. HARDWARE PUSH-TO-TALK (PTT) TRIGGER BUTTONS
 * ========================================================================= */
#define BOARD_PIN_PTT_BOOT      0   /*!< Onboard BOOT Button (Active LOW, internal pull-up) */
#define BOARD_PIN_PTT_EXT       1   /*!< External Push-to-Talk Header Pin (Active LOW, pull-up) */

/* =========================================================================
 * 5. RESERVED SPI2 DISPLAY BUS (ST7789 / ST7735 Future Zero-Rewiring Header)
 * ========================================================================= */
#define BOARD_PIN_DISP_DC       7   /*!< SPI Display Data/Command Control */
#define BOARD_PIN_DISP_RST      8   /*!< SPI Display Hardware Reset */
#define BOARD_PIN_DISP_BLK      9   /*!< SPI Display Backlight PWM Control */
#define BOARD_PIN_DISP_CS       10  /*!< SPI Display Chip Select (FSPI / SPI2_CS0) */
#define BOARD_PIN_DISP_MOSI     11  /*!< SPI Display Master-Out-Slave-In */
#define BOARD_PIN_DISP_SCK      12  /*!< SPI Display Serial Clock */
#define BOARD_PIN_DISP_MISO     13  /*!< SPI Display Master-In-Slave-Out */

/* =========================================================================
 * 6. COMPILE-TIME PIN COLLISION STATIC ASSERTIONS
 * ========================================================================= */

/* USB PHY Pin Sanity */
_Static_assert(BOARD_PIN_USB_DM != BOARD_PIN_USB_DP, "USB D- and D+ pins must be distinct");

/* WS2812 LED Isolation */
_Static_assert(BOARD_PIN_WS2812 != BOARD_PIN_USB_DM, "WS2812 collision with USB D-");
_Static_assert(BOARD_PIN_WS2812 != BOARD_PIN_USB_DP, "WS2812 collision with USB D+");

/* I2S Bus Internal Uniqueness */
_Static_assert(BOARD_PIN_I2S_BCLK != BOARD_PIN_I2S_WS, "I2S BCLK and WS pins collide");
_Static_assert(BOARD_PIN_I2S_BCLK != BOARD_PIN_I2S_DIN, "I2S BCLK and DIN pins collide");
_Static_assert(BOARD_PIN_I2S_WS   != BOARD_PIN_I2S_DIN, "I2S WS and DIN pins collide");

/* I2S vs USB & LED Isolation */
_Static_assert(BOARD_PIN_I2S_BCLK != BOARD_PIN_WS2812, "I2S BCLK collision with WS2812");
_Static_assert(BOARD_PIN_I2S_WS   != BOARD_PIN_WS2812, "I2S WS collision with WS2812");
_Static_assert(BOARD_PIN_I2S_DIN  != BOARD_PIN_WS2812, "I2S DIN collision with WS2812");
_Static_assert(BOARD_PIN_I2S_BCLK != BOARD_PIN_USB_DM, "I2S BCLK collision with USB D-");
_Static_assert(BOARD_PIN_I2S_BCLK != BOARD_PIN_USB_DP, "I2S BCLK collision with USB D+");
_Static_assert(BOARD_PIN_I2S_WS   != BOARD_PIN_USB_DM, "I2S WS collision with USB D-");
_Static_assert(BOARD_PIN_I2S_WS   != BOARD_PIN_USB_DP, "I2S WS collision with USB D+");
_Static_assert(BOARD_PIN_I2S_DIN  != BOARD_PIN_USB_DM, "I2S DIN collision with USB D-");
_Static_assert(BOARD_PIN_I2S_DIN  != BOARD_PIN_USB_DP, "I2S DIN collision with USB D+");

/* PTT Button Isolation */
_Static_assert(BOARD_PIN_PTT_BOOT != BOARD_PIN_PTT_EXT, "PTT BOOT and EXT buttons collide");
_Static_assert(BOARD_PIN_PTT_BOOT != BOARD_PIN_I2S_BCLK && BOARD_PIN_PTT_BOOT != BOARD_PIN_I2S_WS && BOARD_PIN_PTT_BOOT != BOARD_PIN_I2S_DIN, "PTT BOOT collides with I2S");
_Static_assert(BOARD_PIN_PTT_EXT  != BOARD_PIN_I2S_BCLK && BOARD_PIN_PTT_EXT  != BOARD_PIN_I2S_WS && BOARD_PIN_PTT_EXT  != BOARD_PIN_I2S_DIN, "PTT EXT collides with I2S");
_Static_assert(BOARD_PIN_PTT_BOOT != BOARD_PIN_WS2812   && BOARD_PIN_PTT_EXT  != BOARD_PIN_WS2812, "PTT button collides with WS2812");

/* Reserved SPI2 Display Pin Uniqueness */
_Static_assert(BOARD_PIN_DISP_DC   != BOARD_PIN_DISP_RST,  "Display DC and RST collide");
_Static_assert(BOARD_PIN_DISP_DC   != BOARD_PIN_DISP_BLK,  "Display DC and BLK collide");
_Static_assert(BOARD_PIN_DISP_DC   != BOARD_PIN_DISP_CS,   "Display DC and CS collide");
_Static_assert(BOARD_PIN_DISP_DC   != BOARD_PIN_DISP_MOSI, "Display DC and MOSI collide");
_Static_assert(BOARD_PIN_DISP_DC   != BOARD_PIN_DISP_SCK,  "Display DC and SCK collide");
_Static_assert(BOARD_PIN_DISP_DC   != BOARD_PIN_DISP_MISO, "Display DC and MISO collide");
_Static_assert(BOARD_PIN_DISP_RST  != BOARD_PIN_DISP_BLK,  "Display RST and BLK collide");
_Static_assert(BOARD_PIN_DISP_RST  != BOARD_PIN_DISP_CS,   "Display RST and CS collide");
_Static_assert(BOARD_PIN_DISP_RST  != BOARD_PIN_DISP_MOSI, "Display RST and MOSI collide");
_Static_assert(BOARD_PIN_DISP_RST  != BOARD_PIN_DISP_SCK,  "Display RST and SCK collide");
_Static_assert(BOARD_PIN_DISP_RST  != BOARD_PIN_DISP_MISO, "Display RST and MISO collide");
_Static_assert(BOARD_PIN_DISP_CS   != BOARD_PIN_DISP_MOSI, "Display CS and MOSI collide");
_Static_assert(BOARD_PIN_DISP_CS   != BOARD_PIN_DISP_SCK,  "Display CS and SCK collide");
_Static_assert(BOARD_PIN_DISP_CS   != BOARD_PIN_DISP_MISO, "Display CS and MISO collide");
_Static_assert(BOARD_PIN_DISP_MOSI != BOARD_PIN_DISP_SCK,  "Display MOSI and SCK collide");
_Static_assert(BOARD_PIN_DISP_MOSI != BOARD_PIN_DISP_MISO, "Display MOSI and MISO collide");
_Static_assert(BOARD_PIN_DISP_SCK  != BOARD_PIN_DISP_MISO, "Display SCK and MISO collide");

/* Reserved SPI2 Display vs Audio/USB/PTT Subsystems */
_Static_assert(BOARD_PIN_DISP_DC   != BOARD_PIN_I2S_BCLK && BOARD_PIN_DISP_DC   != BOARD_PIN_I2S_WS && BOARD_PIN_DISP_DC   != BOARD_PIN_I2S_DIN, "Display DC collides with I2S");
_Static_assert(BOARD_PIN_DISP_RST  != BOARD_PIN_I2S_BCLK && BOARD_PIN_DISP_RST  != BOARD_PIN_I2S_WS && BOARD_PIN_DISP_RST  != BOARD_PIN_I2S_DIN, "Display RST collides with I2S");
_Static_assert(BOARD_PIN_DISP_BLK  != BOARD_PIN_I2S_BCLK && BOARD_PIN_DISP_BLK  != BOARD_PIN_I2S_WS && BOARD_PIN_DISP_BLK  != BOARD_PIN_I2S_DIN, "Display BLK collides with I2S");
_Static_assert(BOARD_PIN_DISP_CS   != BOARD_PIN_I2S_BCLK && BOARD_PIN_DISP_CS   != BOARD_PIN_I2S_WS && BOARD_PIN_DISP_CS   != BOARD_PIN_I2S_DIN, "Display CS collides with I2S");
_Static_assert(BOARD_PIN_DISP_MOSI != BOARD_PIN_I2S_BCLK && BOARD_PIN_DISP_MOSI != BOARD_PIN_I2S_WS && BOARD_PIN_DISP_MOSI != BOARD_PIN_I2S_DIN, "Display MOSI collides with I2S");
_Static_assert(BOARD_PIN_DISP_SCK  != BOARD_PIN_I2S_BCLK && BOARD_PIN_DISP_SCK  != BOARD_PIN_I2S_WS && BOARD_PIN_DISP_SCK  != BOARD_PIN_I2S_DIN, "Display SCK collides with I2S");
_Static_assert(BOARD_PIN_DISP_MISO != BOARD_PIN_I2S_BCLK && BOARD_PIN_DISP_MISO != BOARD_PIN_I2S_WS && BOARD_PIN_DISP_MISO != BOARD_PIN_I2S_DIN, "Display MISO collides with I2S");

_Static_assert(BOARD_PIN_DISP_CS   != BOARD_PIN_WS2812 && BOARD_PIN_DISP_MOSI != BOARD_PIN_WS2812, "Display collides with WS2812");
_Static_assert(BOARD_PIN_DISP_SCK  != BOARD_PIN_WS2812 && BOARD_PIN_DISP_MISO != BOARD_PIN_WS2812, "Display collides with WS2812");

#ifdef __cplusplus
}
#endif
