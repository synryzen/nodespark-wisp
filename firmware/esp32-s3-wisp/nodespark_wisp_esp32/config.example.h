#pragma once

// Copy this file to config.h before compiling.
// Keep config.h private because it contains your Wi-Fi password.

#define WISP_WIFI_SSID "Your WiFi Name"
#define WISP_WIFI_PASSWORD "Your WiFi Password"

// Use your NodeSparkHub URL. This can be a local LAN URL with a port or
// a remote HTTPS domain through Cloudflare.
// Examples: "http://192.168.1.241:8787" or "https://your-domain.com"
#define WISP_HUB_URL "http://192.168.1.241:8787"

#define WISP_DEVICE_NAME "NodeSpark Wisp ESP32"
#define WISP_DEFAULT_WORKFLOW "Wisp Assistant"

// Experimental on ESP32-S3: the BLE stack can cause reboot loops on some
// Arduino-ESP32 2.x builds when combined with Wi-Fi, HTTPS, I2S, and TFT UI.
// Keep disabled for stable demos. Use the Raspberry Pi Wisp for BLE bridge
// demos until the ESP32 build moves to a lighter BLE stack.
#define WISP_ENABLE_BLE 0

// Keep audio disabled until the MAX98357 amp and INMP441 mic are wired. This
// avoids initializing I2S pins that may be floating during display-only tests.
#define WISP_ENABLE_AUDIO 0

// Hardware bring-up stability defaults. Connect manually from Set > Conn after
// the display/touch wiring is stable.
#define WISP_TFT_RST_PIN 8
#define WISP_CONNECT_ON_BOOT 0
#define WISP_ENABLE_BACKGROUND_HUB_POLL 0
#define WISP_HTTP_TIMEOUT_MS 2500
#define WISP_WIFI_CONNECT_TIMEOUT_MS 15000
#define WISP_TOUCH_POLL_MS 35
#define WISP_WIFI_TX_POWER WIFI_POWER_11dBm
#define WISP_ENABLE_SD 1
#define WISP_SD_CS_PIN 14
#define WISP_ASYNC_WORKFLOWS 1

// Touch calibration varies by 2.8 inch ILI9341/XPT2046 module.
// If touches are reversed or offset, adjust these values after first boot.
#define WISP_TOUCH_MIN_X 240
#define WISP_TOUCH_MAX_X 3800
#define WISP_TOUCH_MIN_Y 320
#define WISP_TOUCH_MAX_Y 3700
