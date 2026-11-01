#include "usb_hid_trigger.h"
#include "board_pins.h"
#include "ws2812_animations.h"
#include "haptics_engine.h"
#include "esp_log.h"
#include "esp_wifi.h"
#include "esp_now.h"
#include "esp_netif.h"
#include "nvs_flash.h"
#include "esp_timer.h"
#include "led_strip.h"

// TinyUSB headers
#include "tinyusb.h"
#include "tusb.h"
#include "class/hid/hid_device.h"
#include "class/cdc/cdc_device.h"
#include "class/net/net_device.h"

// LwIP headers for NAPT & Forwarding
#include "lwip/opt.h"
#include "lwip/ip4_addr.h"
#include "lwip/ip.h"
#if CONFIG_LWIP_IPV4_NAPT
#include "lwip/lwip_napt.h"
#endif

// System & RTC registers for software download boot
#include "esp_system.h"
#include "soc/soc.h"
#include "soc/rtc_cntl_reg.h"

static const char *TAG = "usb_hid_trigger";

#define WIFI_FAILOVER_SSID      "Ayush"
#define WIFI_FAILOVER_PASSWORD  "alibaba2123"

#define ITF_NUM_HID             0
#define ITF_NUM_CDC             1
#define ITF_NUM_CDC_DATA        2
#define ITF_NUM_TOTAL           3

// Hardware-valid endpoint map for ESP32-S3 DWC2 OTG:
// Hardware supports EP0 + 4 IN endpoints (0x81..0x84) + 4 OUT endpoints (0x01..0x04)
#define EPNUM_HID               0x81
#define EPNUM_CDC_NOTIF         0x82
#define EPNUM_CDC_OUT           0x02
#define EPNUM_CDC_IN            0x83

#ifndef CFG_TUD_HID_EP_BUFSIZE
#define CFG_TUD_HID_EP_BUFSIZE  16
#endif
#ifndef CFG_TUD_CDC_EP_BUFSIZE
#define CFG_TUD_CDC_EP_BUFSIZE  64
#endif

/* =========================================================================
 * 1. TinyUSB Descriptors (HID Keyboard + HID Mouse + CDC ACM Stream)
 * ========================================================================= */

enum {
    REPORT_ID_KEYBOARD = 1,
    REPORT_ID_MOUSE = 2
};

static const uint8_t hid_report_descriptor[] = {
    TUD_HID_REPORT_DESC_KEYBOARD( HID_REPORT_ID(REPORT_ID_KEYBOARD) ),
    TUD_HID_REPORT_DESC_MOUSE( HID_REPORT_ID(REPORT_ID_MOUSE) )
};

uint8_t const *tud_hid_descriptor_report_cb(uint8_t instance)
{
    (void)instance;
    return hid_report_descriptor;
}

uint16_t tud_hid_get_report_cb(uint8_t instance, uint8_t report_id, hid_report_type_t report_type, uint8_t *buffer, uint16_t reqlen)
{
    (void)instance;
    (void)report_id;
    (void)report_type;
    (void)buffer;
    (void)reqlen;
    return 0;
}

void tud_hid_set_report_cb(uint8_t instance, uint8_t report_id, hid_report_type_t report_type, uint8_t const *buffer, uint16_t bufsize)
{
    (void)instance;
    (void)report_id;
    (void)report_type;
    (void)buffer;
    (void)bufsize;
}

#define CONFIG_TOTAL_LEN  (TUD_CONFIG_DESC_LEN + TUD_HID_DESC_LEN + TUD_CDC_DESC_LEN)

static const uint8_t desc_configuration[] = {
    // Config number, interface count, string index, total length, attribute, power in mA
    TUD_CONFIG_DESCRIPTOR(1, ITF_NUM_TOTAL, 0, CONFIG_TOTAL_LEN, TUSB_DESC_CONFIG_ATT_REMOTE_WAKEUP, 500),

    // Interface 0: HID Composite (Keyboard + Mouse)
    TUD_HID_DESCRIPTOR(ITF_NUM_HID, 4, HID_ITF_PROTOCOL_NONE, sizeof(hid_report_descriptor), EPNUM_HID, CFG_TUD_HID_EP_BUFSIZE, 10),

    // Interface 1 & 2: CDC ACM Stream
    TUD_CDC_DESCRIPTOR(ITF_NUM_CDC, 5, EPNUM_CDC_NOTIF, 8, EPNUM_CDC_OUT, EPNUM_CDC_IN, CFG_TUD_CDC_EP_BUFSIZE)
};

static const tusb_desc_device_t desc_device = {
    .bLength            = sizeof(tusb_desc_device_t),
    .bDescriptorType    = TUSB_DESC_DEVICE,
    .bcdUSB             = 0x0200,
    .bDeviceClass       = TUSB_CLASS_MISC,
    .bDeviceSubClass    = MISC_SUBCLASS_COMMON,
    .bDeviceProtocol    = MISC_PROTOCOL_IAD,
    .bMaxPacketSize0    = CFG_TUD_ENDPOINT0_SIZE,
    .idVendor           = 0x303A,
    .idProduct          = 0x4003,
    .bcdDevice          = 0x0101,
    .iManufacturer      = 0x01,
    .iProduct           = 0x02,
    .iSerialNumber      = 0x03,
    .bNumConfigurations = 0x01
};

static char const *string_desc_arr[] = {
    (const char[]){0x09, 0x04},                    // 0: Supported language (English 0x0409)
    "Waveshare",                                    // 1: Manufacturer
    "ESP32-S3-Zero Xiaozhi Acoustic Node",         // 2: Product
    "S3ZERO-A7-PTT-001",                            // 3: Serial
    "Xiaozhi HID Trigger",                          // 4: HID Interface
    "Xiaozhi Audio & Control Stream",               // 5: CDC Interface
    "S3 Emergency RNDIS Ethernet"                   // 6: Net Interface
};

/* =========================================================================
 * 2. WS2812 Visual Telemetry LED Driver
 * =========================================================================
 * Delegated to ws2812_animations.c for organic shader routines & audio reactive aura.
 */

/* =========================================================================
 * 3. HID Keyboard & Mouse Keystroke / Scroll Dispatcher
 * ========================================================================= */

#define MODIFIER_CTRL_SHIFT_ALT (KEYBOARD_MODIFIER_LEFTCTRL | KEYBOARD_MODIFIER_LEFTSHIFT | KEYBOARD_MODIFIER_LEFTALT)

void usb_hid_send_wake_keystroke(void)
{
    ws2812_flash_wake();
    if (!tud_hid_ready()) {
        ESP_LOGW(TAG, "TinyUSB HID not ready to dispatch Wake keystroke");
        return;
    }

    uint8_t keycode[6] = {HID_KEY_W, 0, 0, 0, 0, 0};
    tud_hid_keyboard_report(REPORT_ID_KEYBOARD, MODIFIER_CTRL_SHIFT_ALT, keycode);
    vTaskDelay(pdMS_TO_TICKS(40));

    uint8_t empty_keys[6] = {0};
    tud_hid_keyboard_report(REPORT_ID_KEYBOARD, 0, empty_keys);
    ESP_LOGI(TAG, "Sent Wake Keystroke: Ctrl+Shift+Alt+W to host Pi");
}

void usb_hid_send_ptt_keystroke(bool pressed)
{
    if (!tud_hid_ready()) return;

    if (pressed) {
        uint8_t keycode[6] = {HID_KEY_P, 0, 0, 0, 0, 0};
        tud_hid_keyboard_report(REPORT_ID_KEYBOARD, MODIFIER_CTRL_SHIFT_ALT, keycode);
        ESP_LOGI(TAG, "Sent PTT Keydown: Ctrl+Shift+Alt+P");
    } else {
        uint8_t empty_keys[6] = {0};
        tud_hid_keyboard_report(REPORT_ID_KEYBOARD, 0, empty_keys);
        ESP_LOGI(TAG, "Sent PTT Keyup");
    }
}

void usb_hid_send_mouse_scroll(int8_t scroll_step)
{
    if (!tud_hid_ready()) return;
    tud_hid_mouse_report(REPORT_ID_MOUSE, 0, 0, 0, scroll_step, 0);
}

/* =========================================================================
 * 4. USB CDC Audio Streaming & Host Command Parser
 * ========================================================================= */

void usb_stream_audio_frame(const int16_t *pcm_mono, size_t samples)
{
    if (!tud_cdc_connected()) return;

    // Calculate energy downsampled for WS2812 dynamic aurora reaction
    if (pcm_mono && samples > 0) {
        int32_t sum = 0;
        size_t step = (samples > 120) ? (samples / 120) : 1;
        size_t count = 0;
        for (size_t i = 0; i < samples; i += step) {
            sum += abs((int)pcm_mono[i]);
            count++;
        }
        if (count > 0) {
            int32_t avg = sum / (int32_t)count;
            int energy = (avg >> 5); // 0..255 scale
            if (energy > 255) energy = 255;
            ws2812_set_audio_energy((uint8_t)energy);
        }
    }

    uint16_t byte_len = samples * sizeof(int16_t);
    uint8_t header[4] = {
        0xAA, 0x55,
        (uint8_t)(byte_len & 0xFF),
        (uint8_t)((byte_len >> 8) & 0xFF)
    };

    uint32_t avail = tud_cdc_write_available();
    if (avail >= (sizeof(header) + byte_len)) {
        tud_cdc_write(header, sizeof(header));
        tud_cdc_write(pcm_mono, byte_len);
        tud_cdc_write_flush();
    }
}

static void process_host_command(const char *cmd_line);

static void cdc_rx_task(void *pvParameters)
{
    char line_buf[256];
    size_t line_pos = 0;

    while (1) {
        if (tud_cdc_available()) {
            uint8_t ch;
            while (tud_cdc_read(&ch, 1) == 1) {
                if (ch == '\n' || ch == '\r') {
                    if (line_pos > 0) {
                        line_buf[line_pos] = '\0';
                        process_host_command(line_buf);
                        line_pos = 0;
                    }
                } else if (line_pos < sizeof(line_buf) - 1) {
                    line_buf[line_pos++] = (char)ch;
                }
            }
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

static void process_host_command(const char *cmd_line)
{
    while (*cmd_line == ' ' || *cmd_line == '\t' || *cmd_line == '\r') cmd_line++;
    ESP_LOGI(TAG, "Received CDC Command: %s", cmd_line);

    if (strcmp(cmd_line, "CMD:NET_ENABLE") == 0 || strcmp(cmd_line, "s3_net_on") == 0) {
        rndis_failover_start();
        tud_cdc_write_str("RNDIS_STATUS:STARTING\n");
        tud_cdc_write_flush();
    } else if (strcmp(cmd_line, "CMD:NET_DISABLE") == 0 || strcmp(cmd_line, "s3_net_off") == 0) {
        rndis_failover_stop();
        tud_cdc_write_str("RNDIS_STATUS:DISABLED\n");
        tud_cdc_write_flush();
    } else if (strcmp(cmd_line, "CMD:NET_STATUS") == 0) {
        if (rndis_is_active()) {
            tud_cdc_write_str("RNDIS_STATUS:ACTIVE\n");
        } else {
            tud_cdc_write_str("RNDIS_STATUS:IDLE\n");
        }
        tud_cdc_write_flush();
    } else if (strncmp(cmd_line, "CMD:ROVER_DRIVE", 15) == 0) {
        int vx = 0, vy = 0, omega = 0, btn = 0, flags = 0;
        if (sscanf(cmd_line + 15, "%d %d %d %d %d", &vx, &vy, &omega, &btn, &flags) >= 3) {
            espnow_rover_send_command((int8_t)vx, (int8_t)vy, (int8_t)omega, (uint8_t)btn, (uint8_t)flags);
        }
    } else if (strcmp(cmd_line, "CMD:ROVER_ESTOP") == 0) {
        espnow_rover_send_command(0, 0, 0, 0x08, 0); // E-stop button flag
        haptics_play(HAPTIC_ALERT, 220, 240);
        tud_cdc_write_str("ROVER:ESTOP_TRIGGERED\n");
        tud_cdc_write_flush();
    } else if (strncmp(cmd_line, "CMD:ROVER_GIMBAL", 16) == 0) {
        int pan = 1500, tilt = 1500;
        if (sscanf(cmd_line + 16, "%d %d", &pan, &tilt) >= 2) {
            char gcmd[48];
            snprintf(gcmd, sizeof(gcmd), "GIMBAL_ACK:%d,%d\n", pan, tilt);
            tud_cdc_write_str(gcmd);
            tud_cdc_write_flush();
        }
    } else if (strncmp(cmd_line, "CMD:HAPTIC", 10) == 0) {
        int effect_id = 1;
        int intensity = 180;
        int duration_ms = 0;
        char clean_args[64];
        strncpy(clean_args, cmd_line + 10, sizeof(clean_args) - 1);
        clean_args[sizeof(clean_args) - 1] = '\0';
        for (char *p = clean_args; *p; p++) {
            if (*p == ',' || *p == ':') *p = ' ';
        }
        int matched = sscanf(clean_args, "%d %d %d", &effect_id, &intensity, &duration_ms);
        if (matched >= 1) {
            if (effect_id == 0) {
                haptics_stop();
            } else {
                if (intensity <= 0 || intensity > 255) intensity = 180;
                haptics_play((haptic_effect_t)effect_id, (uint8_t)intensity, (uint16_t)duration_ms);
            }
            char ack[48];
            snprintf(ack, sizeof(ack), "HAPTIC_ACK:%d,%d,%d\n", effect_id, intensity, duration_ms);
            tud_cdc_write_str(ack);
            tud_cdc_write_flush();
        }
    } else if (strncmp(cmd_line, "CMD:LED_PATTERN", 15) == 0) {
        int pattern_id = 0;
        int hue_offset = 0;
        int speed = 100;
        char clean_args[64];
        strncpy(clean_args, cmd_line + 15, sizeof(clean_args) - 1);
        clean_args[sizeof(clean_args) - 1] = '\0';
        for (char *p = clean_args; *p; p++) {
            if (*p == ',' || *p == ':') *p = ' ';
        }
        int matched = sscanf(clean_args, "%d %d %d", &pattern_id, &hue_offset, &speed);
        if (matched >= 1) {
            ws2812_set_pattern((uint8_t)pattern_id, (int16_t)hue_offset, (uint16_t)speed);
            char ack[48];
            snprintf(ack, sizeof(ack), "LED_ACK:%d,%d,%d\n", pattern_id, hue_offset, speed);
            tud_cdc_write_str(ack);
            tud_cdc_write_flush();
        }
    } else if (strcmp(cmd_line, "CMD:BOOTLOADER") == 0 || strcmp(cmd_line, "s3_bootloader") == 0) {
        ESP_LOGW(TAG, "Rebooting into ROM download mode via RTC_CNTL_FORCE_DOWNLOAD_BOOT");
        tud_cdc_write_str("ACK:BOOTLOADER\n");
        tud_cdc_write_flush();
        vTaskDelay(pdMS_TO_TICKS(150));
        REG_WRITE(RTC_CNTL_OPTION1_REG, RTC_CNTL_FORCE_DOWNLOAD_BOOT);
        esp_restart();
    } else if (strcmp(cmd_line, "CMD:PING") == 0) {
        tud_cdc_write_str("PONG:S3_OK\n");
        tud_cdc_write_flush();
    }
}

/* =========================================================================
 * 5. On-Demand RNDIS Wi-Fi NAT Failover (SSID "Ayush" / pass "alibaba2123")
 * ========================================================================= */

static volatile bool s_rndis_active = false;
static esp_netif_t *s_sta_netif = NULL;
static esp_netif_t *s_usb_netif = NULL;
static uint32_t s_sta_ip = 0;

uint8_t tud_network_mac_address[6] = {0x02, 0x02, 0x84, 0x6A, 0x96, 0x00};

static esp_err_t usb_netif_transmit(void *h, void *buffer, size_t len)
{
    (void)h;
    if (tud_network_can_xmit((uint16_t)len)) {
        tud_network_xmit(buffer, (uint16_t)len);
        return ESP_OK;
    }
    return ESP_FAIL;
}

static void wifi_event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        ESP_LOGI(TAG, "Wi-Fi STA started");
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        if (s_rndis_active) {
            ESP_LOGW(TAG, "Wi-Fi disconnected. Reconnecting to SSID %s...", WIFI_FAILOVER_SSID);
            esp_wifi_connect();
        }
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
        ESP_LOGI(TAG, "Connected to %s! Got IP: " IPSTR, WIFI_FAILOVER_SSID, IP2STR(&event->ip_info.ip));

#if CONFIG_LWIP_IPV4_NAPT
        s_sta_ip = event->ip_info.ip.addr;
        ip_napt_enable(s_sta_ip, 1);
        ESP_LOGI(TAG, "LwIP IPv4 NAPT enabled on station netif");
#endif
        s_rndis_active = true;
        ws2812_set_mode(LED_MODE_RNDIS);
        tud_network_link_state(0, true);

        if (tud_cdc_connected()) {
            char notify[64];
            snprintf(notify, sizeof(notify), "RNDIS_STATUS:ONLINE ip=" IPSTR "\n", IP2STR(&event->ip_info.ip));
            tud_cdc_write_str(notify);
            tud_cdc_write_flush();
        }
    }
}

esp_err_t rndis_failover_start(void)
{
    if (s_rndis_active) {
        ESP_LOGI(TAG, "RNDIS failover already active");
        return ESP_OK;
    }

    ESP_LOGI(TAG, "Starting Emergency S3 RNDIS Failover -> SSID: %s", WIFI_FAILOVER_SSID);

    wifi_config_t wifi_cfg = {
        .sta = {
            .ssid = WIFI_FAILOVER_SSID,
            .password = WIFI_FAILOVER_PASSWORD,
            .threshold.authmode = WIFI_AUTH_WPA2_PSK,
        },
    };

    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_cfg));
    ESP_ERROR_CHECK(esp_wifi_connect());

    return ESP_OK;
}

esp_err_t rndis_failover_stop(void)
{
    if (!s_rndis_active) return ESP_OK;

    ESP_LOGI(TAG, "Deactivating RNDIS Failover -> Restoring default state");
    s_rndis_active = false;
    tud_network_link_state(0, false);

#if CONFIG_LWIP_IPV4_NAPT
    if (s_sta_ip) {
        ip_napt_enable(s_sta_ip, 0);
        s_sta_ip = 0;
    }
#endif

    // Disconnect STA without calling esp_wifi_stop(), so ESP-NOW radio stays active!
    esp_wifi_disconnect();

    ws2812_set_mode(LED_MODE_IDLE);
    return ESP_OK;
}

bool rndis_is_active(void)
{
    return s_rndis_active;
}

void tud_network_init_cb(void)
{
    ESP_LOGI(TAG, "TinyUSB Network Subsystem Initialized");
}

bool tud_network_recv_cb(const uint8_t *src, uint16_t size)
{
    if (s_usb_netif && s_rndis_active) {
        esp_netif_receive(s_usb_netif, (void *)src, size, NULL);
    }
    tud_network_recv_renew();
    return true;
}

uint16_t tud_network_xmit_cb(uint8_t *dst, void *ref, uint16_t arg)
{
    memcpy(dst, ref, arg);
    return arg;
}

/* =========================================================================
 * 6. ESP-NOW Mecanum Rover Gateway (Magic = 0xA7)
 * ========================================================================= */

static uint8_t s_rover_seq = 0;
static const uint8_t s_broadcast_mac[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

static uint8_t compute_crc(const void *data, size_t len)
{
    const uint8_t *p = (const uint8_t *)data;
    uint8_t crc = 0xAA;
    for (size_t i = 0; i < len; i++) {
        crc ^= p[i];
    }
    return crc;
}

static void espnow_recv_cb(const esp_now_recv_info_t *recv_info, const uint8_t *data, int len)
{
    if (len >= 12 && data[0] == ROVER_PACKET_MAGIC) {
        if (len == sizeof(rover_pkt_t)) {
            const rover_pkt_t *ack = (const rover_pkt_t *)data;
            if (tud_cdc_connected()) {
                char out[128];
                snprintf(out, sizeof(out), "ROVER_ACK:{\"seq\":%u,\"type\":%u,\"mode\":%u,\"speed\":%u,\"flags\":%u,\"heading\":%d}\n",
                         ack->seq, ack->type, ack->mode, ack->speed, ack->flags, ack->arg);
                tud_cdc_write_str(out);
                tud_cdc_write_flush();
            }
        } else if (len == sizeof(rover_telem_packet_t)) {
            const rover_telem_packet_t *telem = (const rover_telem_packet_t *)data;
            uint8_t exp_crc = compute_crc(telem, sizeof(rover_telem_packet_t) - 1);
            if (telem->crc == exp_crc && tud_cdc_connected()) {
                char out[128];
                snprintf(out, sizeof(out), "ROVER_TELEM:{\"seq\":%u,\"bat\":%u,\"tof_f\":%d,\"tof_r\":%d,\"status\":%u}\n",
                         telem->seq, telem->battery_pct, telem->tof_front_mm, telem->tof_rear_mm, telem->status_flags);
                tud_cdc_write_str(out);
                tud_cdc_write_flush();
            }
        }
    }
}

esp_err_t espnow_rover_init(void)
{
    ESP_LOGI(TAG, "Initializing ESP-NOW Mecanum Rover Transceiver (Magic: 0xA7)");

    esp_err_t ret = esp_now_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "esp_now_init failed: %s", esp_err_to_name(ret));
        return ret;
    }

    esp_now_register_recv_cb(espnow_recv_cb);

    esp_now_peer_info_t peer_info = {
        .channel = 0,
        .ifidx = WIFI_IF_STA,
        .encrypt = false
    };
    memcpy(peer_info.peer_addr, s_broadcast_mac, 6);

    ret = esp_now_add_peer(&peer_info);
    if (ret != ESP_OK && ret != ESP_ERR_ESPNOW_EXIST) {
        ESP_LOGE(TAG, "Failed to register ESP-NOW broadcast peer: %s", esp_err_to_name(ret));
        return ret;
    }

    ESP_LOGI(TAG, "ESP-NOW Rover Channel Gateway ready");
    return ESP_OK;
}

esp_err_t espnow_rover_send_command(int8_t vx, int8_t vy, int8_t omega, uint8_t buttons, uint8_t flags)
{
    rover_pkt_t pkt;
    pkt.magic = ROVER_PACKET_MAGIC;
    pkt.type = (vx == 0 && vy == 0 && omega == 0 && !(flags & 0x01)) ? 4 : 3; // 4=STOP, 3=DRIVE
    pkt.x = vy;     // Strafe: -100 (left) .. +100 (right)
    pkt.y = vx;     // Fwd/Rev: -100 (back) .. +100 (fwd)
    pkt.z = omega;  // Rotation: -100 (CCW) .. +100 (CW)
    pkt.speed = 70; // 70% duty ceiling
    pkt.mode = 1;   // MODE_NORMAL
    pkt.flags = flags;
    pkt.arg = 0;
    pkt.seq = s_rover_seq++;

    esp_err_t ret = esp_now_send(s_broadcast_mac, (const uint8_t *)&pkt, sizeof(pkt));
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "ESP-NOW command transmit failed: %s", esp_err_to_name(ret));
    }
    return ret;
}

/* =========================================================================
 * 7. Composite USB Subsystem Initialization
 * ========================================================================= */

static esp_netif_driver_ifconfig_t s_usb_driver_cfg = {
    .handle = (void *)1,
    .transmit = usb_netif_transmit,
};

static void __attribute__((unused)) init_usb_netif(void)
{
    esp_netif_inherent_config_t base_cfg = {
        .flags = ESP_NETIF_DHCP_SERVER | ESP_NETIF_FLAG_AUTOUP | ESP_NETIF_FLAG_IS_PPP,
        .if_key = "USB_RNDIS",
        .if_desc = "usb_rndis_netif",
        .route_prio = 10,
    };
    esp_netif_config_t cfg = {
        .base = &base_cfg,
        .stack = ESP_NETIF_NETSTACK_DEFAULT_ETH,
        .driver = &s_usb_driver_cfg,
    };
    s_usb_netif = esp_netif_new(&cfg);
    if (!s_usb_netif) {
        ESP_LOGE(TAG, "Failed to create USB netif");
        return;
    }

    // Set static IP: 192.168.4.1/24 with DHCP server for connected Pi host
    esp_netif_ip_info_t ip_info;
    IP4_ADDR(&ip_info.ip, 192, 168, 4, 1);
    IP4_ADDR(&ip_info.gw, 192, 168, 4, 1);
    IP4_ADDR(&ip_info.netmask, 255, 255, 255, 0);

    esp_netif_dhcps_stop(s_usb_netif);
    esp_netif_set_ip_info(s_usb_netif, &ip_info);
    esp_netif_dhcps_start(s_usb_netif);
    esp_netif_action_start(s_usb_netif, NULL, 0, NULL);
    esp_netif_action_connected(s_usb_netif, NULL, 0, NULL);

    ESP_LOGI(TAG, "USB Netif configured: IP 192.168.4.1, DHCP server active");
}

esp_err_t usb_hid_trigger_init(void)
{
    ESP_LOGI(TAG, "Initializing TinyUSB Composite Stack & WS2812 Telemetry");

    // Clear force download boot flag if set from earlier session
    REG_CLR_BIT(RTC_CNTL_OPTION1_REG, RTC_CNTL_FORCE_DOWNLOAD_BOOT);

    // 1. Initialize WS2812 RGB LED & Animation Task
    ws2812_animations_init();

    // 2. Initialize Wi-Fi Netif & Event Handlers
    esp_netif_init();
    esp_event_loop_create_default();
    s_sta_netif = esp_netif_create_default_wifi_sta();
    
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL, NULL));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL, NULL));

    // 3. Initialize ESP-NOW Rover Gateway (Requires Wi-Fi to be started!)
    espnow_rover_init();

    // 4. Initialize TinyUSB Driver
    tinyusb_config_t const tusb_cfg = {
        .device_descriptor = &desc_device,
        .string_descriptor = string_desc_arr,
        .string_descriptor_count = sizeof(string_desc_arr) / sizeof(string_desc_arr[0]),
        .external_phy = false,
        .configuration_descriptor = desc_configuration,
    };

    esp_err_t ret = tinyusb_driver_install(&tusb_cfg);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "tinyusb_driver_install failed: %s", esp_err_to_name(ret));
        return ret;
    }

    // Spawn CDC receive task on Core 0
    xTaskCreatePinnedToCore(cdc_rx_task, "cdc_rx", 3072, NULL, 4, NULL, 0);

    ESP_LOGI(TAG, "TinyUSB Composite Stack online (VID: 0x303A, PID: 0x4002)");
    return ESP_OK;
}
