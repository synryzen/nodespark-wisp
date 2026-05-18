#pragma once

// Copy this file to config.h before compiling.
// The Core2 also saves pairing token, volume, and workflow preferences in NVS.

#define WISP_WIFI_SSID "Your WiFi Name"
#define WISP_WIFI_PASSWORD "Your WiFi Password"

// Use your NodeSparkHub LAN URL while local, or your Cloudflare URL when remote.
// Examples:
//   "http://192.168.1.241:8787"
//   "https://nodespark.msidragon.com"
#define WISP_HUB_URL "http://192.168.1.241:8787"

#define WISP_DEVICE_NAME "NodeSpark Wisp Core2"
#define WISP_DEFAULT_WORKFLOW "Wisp Assistant"

#define WISP_CONNECT_ON_BOOT 1
#define WISP_HTTP_TIMEOUT_MS 5000
#define WISP_HUB_HEARTBEAT_MS 45000
#define WISP_COMMAND_POLL_MS 12000

#define WISP_ENABLE_SD 1
#define WISP_ENABLE_MIC 1
#define WISP_ENABLE_SPEAKER 1
#define WISP_ENABLE_HAPTICS 1
