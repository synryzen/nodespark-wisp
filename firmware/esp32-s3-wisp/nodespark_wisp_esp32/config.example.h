#pragma once

// Copy this file to config.h before compiling.
// Keep config.h private because it contains your Wi-Fi password.

#define WISP_WIFI_SSID "Your WiFi Name"
#define WISP_WIFI_PASSWORD "Your WiFi Password"

// Use the LAN URL shown in NodeSparkHub Hub Server settings.
// Example: "http://192.168.1.241:8787"
#define WISP_HUB_URL "http://192.168.1.241:8787"

#define WISP_DEVICE_NAME "NodeSpark Wisp ESP32"
#define WISP_DEFAULT_WORKFLOW "Wisp Assistant"

// Touch calibration varies by 2.8 inch ILI9341/XPT2046 module.
// If touches are reversed or offset, adjust these values after first boot.
#define WISP_TOUCH_MIN_X 240
#define WISP_TOUCH_MAX_X 3800
#define WISP_TOUCH_MIN_Y 320
#define WISP_TOUCH_MAX_Y 3700

