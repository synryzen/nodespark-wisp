#include <Arduino.h>
#include <ArduinoJson.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ILI9341.h>
#include <HTTPClient.h>
#include <Preferences.h>
#include <SPI.h>
#include <SD.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <XPT2046_Touchscreen.h>
#include "driver/i2s.h"
#include "esp_heap_caps.h"
#include "esp_system.h"
#include "mascot_logo.h"

#if __has_include("config.h")
#include "config.h"
#else
#define WISP_WIFI_SSID ""
#define WISP_WIFI_PASSWORD ""
#define WISP_HUB_URL "http://192.168.1.241:8787"
#define WISP_DEVICE_NAME "NodeSpark Wisp ESP32"
#define WISP_DEFAULT_WORKFLOW "Wisp Assistant"
#define WISP_TOUCH_MIN_X 240
#define WISP_TOUCH_MAX_X 3800
#define WISP_TOUCH_MIN_Y 320
#define WISP_TOUCH_MAX_Y 3700
#endif

#ifndef WISP_ENABLE_BLE
#define WISP_ENABLE_BLE 0
#endif
#ifndef WISP_ENABLE_AUDIO
#define WISP_ENABLE_AUDIO 0
#endif
#ifndef WISP_TFT_RST_PIN
#define WISP_TFT_RST_PIN 8
#endif
#ifndef WISP_CONNECT_ON_BOOT
#define WISP_CONNECT_ON_BOOT 0
#endif
#ifndef WISP_ENABLE_BACKGROUND_HUB_POLL
#define WISP_ENABLE_BACKGROUND_HUB_POLL 1
#endif
#ifndef WISP_ENABLE_HUB_HEARTBEAT
#define WISP_ENABLE_HUB_HEARTBEAT 1
#endif
#ifndef WISP_HUB_HEARTBEAT_MS
#define WISP_HUB_HEARTBEAT_MS 45000
#endif
#ifndef WISP_COMMAND_POLL_MS
#define WISP_COMMAND_POLL_MS 15000
#endif
#ifndef WISP_HTTP_TIMEOUT_MS
#define WISP_HTTP_TIMEOUT_MS 2500
#endif
#ifndef WISP_MAX_SPEECH_WAV_BYTES
#define WISP_MAX_SPEECH_WAV_BYTES 1572864
#endif
#ifndef WISP_WIFI_CONNECT_TIMEOUT_MS
#define WISP_WIFI_CONNECT_TIMEOUT_MS 7000
#endif
#ifndef WISP_TOUCH_POLL_MS
#define WISP_TOUCH_POLL_MS 35
#endif
#ifndef WISP_WIFI_TX_POWER
#define WISP_WIFI_TX_POWER WIFI_POWER_5dBm
#endif
#ifndef WISP_ENABLE_SD
#define WISP_ENABLE_SD 1
#endif
#ifndef WISP_SD_CS_PIN
#define WISP_SD_CS_PIN 14
#endif
#ifndef WISP_ASYNC_WORKFLOWS
#define WISP_ASYNC_WORKFLOWS 1
#endif
#ifndef WISP_FORCE_DEFAULT_AMP_PINS
#define WISP_FORCE_DEFAULT_AMP_PINS 0
#endif
#if WISP_ENABLE_BLE
#include <BLE2902.h>
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#endif

// ESP32-S3 DevKit pin plan. Change here if your board labels differ.
static constexpr int PIN_TFT_CS = 10;
static constexpr int PIN_TFT_DC = 9;
static constexpr int PIN_TFT_RST = WISP_TFT_RST_PIN;
static constexpr int PIN_SPI_MOSI = 11;
static constexpr int PIN_SPI_MISO = 13;
static constexpr int PIN_SPI_SCK = 12;
static constexpr int PIN_TOUCH_CS = 7;
static constexpr int PIN_TOUCH_IRQ = 6;
static constexpr int PIN_SD_CS = WISP_SD_CS_PIN;

static constexpr int PIN_AMP_BCLK = 4;
static constexpr int PIN_AMP_LRCLK = 5;
static constexpr int PIN_AMP_DIN = 16;

static constexpr int PIN_MIC_SCK = 15;
static constexpr int PIN_MIC_WS = 17;
static constexpr int PIN_MIC_SD = 18;

static constexpr int SCREEN_W = 320;
static constexpr int SCREEN_H = 240;
static constexpr const char* APP_VERSION = "nodespark-wisp-esp32/0.1.0";
#if WISP_ENABLE_BLE
static constexpr const char* BLE_SERVICE_UUID = "4E530001-4E53-5749-5350-000000000001";
static constexpr const char* BLE_COMMAND_UUID = "4E530002-4E53-5749-5350-000000000001";
static constexpr const char* BLE_EVENT_UUID = "4E530003-4E53-5749-5350-000000000001";
static constexpr const char* BLE_STATE_UUID = "4E530004-4E53-5749-5350-000000000001";
#endif

static constexpr int TL_DATUM = 0;
static constexpr int ML_DATUM = 1;
static constexpr int MR_DATUM = 2;
static constexpr int MC_DATUM = 3;

struct AmpPinMap {
  int bclk;
  int lrclk;
  int din;
  const char* label;
};

static const AmpPinMap AMP_PIN_MAPS[] = {
  {PIN_AMP_BCLK, PIN_AMP_LRCLK, PIN_AMP_DIN, "B4 L5 D16"},
  {PIN_AMP_LRCLK, PIN_AMP_BCLK, PIN_AMP_DIN, "B5 L4 D16"},
  {PIN_AMP_BCLK, PIN_AMP_DIN, PIN_AMP_LRCLK, "B4 L16 D5"},
  {PIN_AMP_DIN, PIN_AMP_LRCLK, PIN_AMP_BCLK, "B16 L5 D4"},
  {PIN_AMP_LRCLK, PIN_AMP_DIN, PIN_AMP_BCLK, "B5 L16 D4"},
  {PIN_AMP_DIN, PIN_AMP_BCLK, PIN_AMP_LRCLK, "B16 L4 D5"},
};
static constexpr int AMP_PIN_MAP_COUNT = sizeof(AMP_PIN_MAPS) / sizeof(AMP_PIN_MAPS[0]);
static const i2s_channel_fmt_t MIC_CHANNEL_FORMATS[] = {
  I2S_CHANNEL_FMT_ONLY_LEFT,
  I2S_CHANNEL_FMT_ONLY_RIGHT,
  I2S_CHANNEL_FMT_RIGHT_LEFT,
};
static constexpr const char* MIC_CHANNEL_LABELS[] = {"LEFT", "RIGHT", "STEREO"};
static constexpr int MIC_CHANNEL_MODE_COUNT = sizeof(MIC_CHANNEL_FORMATS) / sizeof(MIC_CHANNEL_FORMATS[0]);

class WispTft {
public:
  Adafruit_ILI9341 display;
  int datum = TL_DATUM;
  uint16_t textColor = ILI9341_WHITE;

  WispTft() : display(PIN_TFT_CS, PIN_TFT_DC, PIN_TFT_RST) {}

  void releaseSharedSpiDevices() {
    digitalWrite(PIN_TFT_CS, HIGH);
    digitalWrite(PIN_TOUCH_CS, HIGH);
#if WISP_ENABLE_SD
    digitalWrite(PIN_SD_CS, HIGH);
#endif
  }

  void init() {
    pinMode(PIN_TFT_CS, OUTPUT);
    pinMode(PIN_TOUCH_CS, OUTPUT);
#if WISP_ENABLE_SD
    pinMode(PIN_SD_CS, OUTPUT);
#endif
    releaseSharedSpiDevices();
    SPI.begin(PIN_SPI_SCK, PIN_SPI_MISO, PIN_SPI_MOSI, PIN_TFT_CS);
    display.begin(8000000);
    releaseSharedSpiDevices();
  }

  void guardDisplay() {
    digitalWrite(PIN_TOUCH_CS, HIGH);
#if WISP_ENABLE_SD
    digitalWrite(PIN_SD_CS, HIGH);
#endif
  }
  void setRotation(uint8_t rotation) { guardDisplay(); display.setRotation(rotation); }
  void fillScreen(uint16_t color) { guardDisplay(); display.fillScreen(color); }
  void fillRect(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t color) { guardDisplay(); display.fillRect(x, y, w, h, color); }
  void drawRect(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t color) { guardDisplay(); display.drawRect(x, y, w, h, color); }
  void drawFastVLine(int16_t x, int16_t y, int16_t h, uint16_t color) { guardDisplay(); display.drawFastVLine(x, y, h, color); }
  void fillCircle(int16_t x, int16_t y, int16_t r, uint16_t color) { guardDisplay(); display.fillCircle(x, y, r, color); }
  void fillRoundRect(int16_t x, int16_t y, int16_t w, int16_t h, int16_t r, uint16_t color) { guardDisplay(); display.fillRoundRect(x, y, w, h, r, color); }
  void drawRoundRect(int16_t x, int16_t y, int16_t w, int16_t h, int16_t r, uint16_t color) { guardDisplay(); display.drawRoundRect(x, y, w, h, r, color); }
  void drawRGBBitmap(int16_t x, int16_t y, const uint16_t* bitmap, int16_t w, int16_t h) { guardDisplay(); display.drawRGBBitmap(x, y, bitmap, w, h); }
  void setTextColor(uint16_t color, uint16_t bg = ILI9341_BLACK) {
    guardDisplay();
    textColor = color;
    display.setTextColor(color, bg);
  }
  void setTextDatum(int value) { datum = value; }
  void drawString(const String& text, int16_t x, int16_t y, uint8_t font = 2) {
    guardDisplay();
    uint8_t size = font >= 4 ? 2 : 1;
    int16_t x1, y1;
    uint16_t w, h;
    display.setTextSize(size);
    display.setTextColor(textColor);
    display.getTextBounds(text, 0, 0, &x1, &y1, &w, &h);
    int16_t dx = x;
    int16_t dy = y;
    if (datum == MC_DATUM) {
      dx = x - w / 2;
      dy = y - h / 2;
    } else if (datum == ML_DATUM) {
      dy = y - h / 2;
    } else if (datum == MR_DATUM) {
      dx = x - w;
      dy = y - h / 2;
    }
    display.setCursor(dx, dy);
    display.print(text);
  }
};

WispTft tft;
XPT2046_Touchscreen touch(PIN_TOUCH_CS, PIN_TOUCH_IRQ);
Preferences prefs;
WiFiClient hubPlainClient;
WiFiClientSecure hubSecureClient;

enum Screen { SCREEN_STATUS, SCREEN_PAIR, SCREEN_COMMANDS, SCREEN_DEMO, SCREEN_MIC, SCREEN_SETUP };
Screen currentScreen = SCREEN_STATUS;

enum SetupView { SETUP_MAIN, SETUP_WIFI_LIST, SETUP_INPUT };
enum InputTarget { INPUT_NONE, INPUT_SSID, INPUT_WIFI_PASSWORD, INPUT_HUB_BASE, INPUT_HUB_PORT };
enum WifiConnectState { WIFI_IDLE, WIFI_CONNECTING_SAVED, WIFI_CONNECTING_DEFAULT };
SetupView setupView = SETUP_MAIN;
InputTarget inputTarget = INPUT_NONE;
WifiConnectState wifiConnectState = WIFI_IDLE;

String hubUrl = WISP_HUB_URL;
String hubBase;
String hubPort;
String wifiSsid;
String wifiPassword;
String deviceName = WISP_DEVICE_NAME;
String defaultWorkflow = WISP_DEFAULT_WORKFLOW;
String deviceId;
String token;
String pairCode;
String editBuffer;
String lastCommand = "Waiting for Hub command";
String lastStatus = "Booting";
String pendingApprovalId;
String pendingApprovalTitle;
String pendingApprovalBody;
String scannedSsids[6];
int32_t scannedRssi[6];
int scannedCount = 0;
int keyboardPage = 0;
int audioVolumePercent = 90;
int ampPinMode = 0;
int micChannelMode = 0;
bool ampMuted = false;
int lastMicLevel = 0;
int lastMicBytes = 0;
String lastSdStatus = "SD not checked";

uint32_t lastCheckinMs = 0;
uint32_t lastPollMs = 0;
uint32_t lastWifiDrawMs = 0;
uint32_t lastTouchPollMs = 0;
uint32_t lastMicSampleMs = 0;
uint32_t wifiConnectStartMs = 0;
uint32_t lastWifiStatusMs = 0;
String activeWifiSsid;
String activeWifiPassword;
bool ampReady = false;
bool micReady = false;
bool bleReady = false;
bool ampDriverInstalled = false;
bool micDriverInstalled = false;
bool touchDown = false;
bool actionBusy = false;
uint32_t lastTouchHandledMs = 0;
#if WISP_ENABLE_BLE
BLECharacteristic* bleStateCharacteristic = nullptr;
BLECharacteristic* bleEventCharacteristic = nullptr;
char pendingBleRaw[512] = {0};
volatile bool pendingBleCommand = false;
#endif
static constexpr int NOTIFICATION_LIMIT = 5;
String notificationTitles[NOTIFICATION_LIMIT];
String notificationBodies[NOTIFICATION_LIMIT];
int notificationCount = 0;

struct Button {
  int16_t x;
  int16_t y;
  int16_t w;
  int16_t h;
  String label;
  uint16_t color;
};

struct HubCommand {
  String id;
  String kind;
  String title;
  String body;
  String text;
  String subtitle;
  String detail;
  String style;
  String icon;
  String qrData;
  String workflowName;
  String metricLabel;
  String metricValue;
  String items[6];
  int itemCount = 0;
  int rgbR = -1;
  int rgbG = -1;
  int rgbB = -1;
};

void askAssistant(const String& text);
bool askHubAssistant(const String& text);
bool playSpeechClip(const String& speechPath);
String commandBody(const HubCommand& command, const String& fallback = "");
String commandTitle(const HubCommand& command, const String& fallback);
void playChime(int kind);
void runWorkflow(const String& text);
void executeCommand(const HubCommand& command);
bool configureMicInput();

static const uint16_t C_BG = ILI9341_BLACK;
static const uint16_t C_PANEL = 0x1084;
static const uint16_t C_BLUE = 0x05FF;
static const uint16_t C_PINK = 0xF81F;
static const uint16_t C_GREEN = 0x07E0;
static const uint16_t C_AMBER = 0xFD20;
static const uint16_t C_RED = 0xF9A6;
static const uint16_t C_MUTED = 0x9CF3;

uint16_t readableTextColor(uint16_t bg) {
  uint8_t r = ((bg >> 11) & 0x1F) << 3;
  uint8_t g = ((bg >> 5) & 0x3F) << 2;
  uint8_t b = (bg & 0x1F) << 3;
  uint16_t luminance = (uint16_t)r * 30 + (uint16_t)g * 59 + (uint16_t)b * 11;
  return luminance > 13000 ? ILI9341_BLACK : ILI9341_WHITE;
}

String resetReasonName(esp_reset_reason_t reason) {
  if (reason == ESP_RST_POWERON) return "power on";
  if (reason == ESP_RST_EXT) return "external reset";
  if (reason == ESP_RST_SW) return "software reset";
  if (reason == ESP_RST_PANIC) return "panic";
  if (reason == ESP_RST_INT_WDT) return "interrupt watchdog";
  if (reason == ESP_RST_TASK_WDT) return "task watchdog";
  if (reason == ESP_RST_WDT) return "watchdog";
  if (reason == ESP_RST_DEEPSLEEP) return "deep sleep";
  if (reason == ESP_RST_BROWNOUT) return "brownout";
  if (reason == ESP_RST_SDIO) return "sdio";
  return "unknown " + String((int)reason);
}

String macId() {
  uint64_t mac = ESP.getEfuseMac();
  char buf[24];
  snprintf(buf, sizeof(buf), "ESP32S3-%04X%08X", (uint16_t)(mac >> 32), (uint32_t)mac);
  return String(buf);
}

String deviceUuidFromMac() {
  uint64_t mac = ESP.getEfuseMac();
  char buf[37];
  snprintf(
    buf,
    sizeof(buf),
    "00000000-0000-4000-8000-%04X%08X",
    (uint16_t)(mac >> 32),
    (uint32_t)mac
  );
  return String(buf);
}

String jsonEscape(const String& value) {
  String out;
  for (size_t i = 0; i < value.length(); i++) {
    char c = value[i];
    if (c == '"' || c == '\\') out += '\\';
    if (c == '\n') out += "\\n";
    else out += c;
  }
  return out;
}

String urlEncodePath(String value) {
  const char* hex = "0123456789ABCDEF";
  String out;
  for (size_t i = 0; i < value.length(); i++) {
    uint8_t c = (uint8_t)value[i];
    if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') ||
        c == '-' || c == '_' || c == '.' || c == '~') {
      out += (char)c;
    } else {
      out += '%';
      out += hex[(c >> 4) & 0x0F];
      out += hex[c & 0x0F];
    }
  }
  return out;
}

String normalizedWorkflowName(String value) {
  value.toLowerCase();
  String out;
  for (size_t i = 0; i < value.length(); i++) {
    char c = value[i];
    if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9')) out += c;
  }
  return out;
}

String repeatedChar(char c, int count) {
  String out;
  for (int i = 0; i < count; i++) out += c;
  return out;
}

uint16_t rgb565(int r, int g, int b) {
  r = constrain(r, 0, 255);
  g = constrain(g, 0, 255);
  b = constrain(b, 0, 255);
  return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3);
}

String formatBytes(uint64_t bytes) {
  if (bytes >= 1024ULL * 1024ULL * 1024ULL) return String((double)bytes / (1024.0 * 1024.0 * 1024.0), 1) + " GB";
  if (bytes >= 1024ULL * 1024ULL) return String((double)bytes / (1024.0 * 1024.0), 1) + " MB";
  if (bytes >= 1024ULL) return String((double)bytes / 1024.0, 1) + " KB";
  return String((unsigned long)bytes) + " B";
}

void appendSdLog(const String& event, const String& detail = "") {
#if WISP_ENABLE_SD
  digitalWrite(PIN_TFT_CS, HIGH);
  digitalWrite(PIN_TOUCH_CS, HIGH);
  digitalWrite(PIN_SD_CS, HIGH);
  if (!SD.begin(PIN_SD_CS, SPI, 4000000)) {
    digitalWrite(PIN_SD_CS, HIGH);
    return;
  }
  File file = SD.open("/nodespark_wisp_log.txt", FILE_APPEND);
  if (file) {
    file.print(millis());
    file.print(",");
    file.print(event);
    if (detail.length()) {
      file.print(",");
      String clean = detail;
      clean.replace("\n", " ");
      clean.replace("\r", " ");
      clean.replace(",", ";");
      file.print(clean);
    }
    file.println();
    file.close();
  }
  SD.end();
  digitalWrite(PIN_SD_CS, HIGH);
#endif
}

String clipped(String value, int maxLen) {
  value.trim();
  if ((int)value.length() <= maxLen) return value;
  if (maxLen <= 1) return value.substring(0, maxLen);
  return value.substring(0, maxLen - 1) + "~";
}

String bounded(String value, int maxLen) {
  value.trim();
  if ((int)value.length() > maxLen) value = value.substring(0, maxLen);
  return value;
}

void saveAudioVolume() {
  audioVolumePercent = constrain(audioVolumePercent, 0, 100);
  prefs.putInt("audioVol", audioVolumePercent);
}

void adjustAudioVolume(int delta) {
  audioVolumePercent = constrain(audioVolumePercent + delta, 0, 100);
  saveAudioVolume();
  lastStatus = "Volume " + String(audioVolumePercent) + "%";
}

int currentAmpPinIndex() {
  return constrain(ampPinMode, 0, AMP_PIN_MAP_COUNT - 1);
}

String ampPinModeLabel() {
  return String(AMP_PIN_MAPS[currentAmpPinIndex()].label);
}

int currentMicModeIndex() {
  return constrain(micChannelMode, 0, MIC_CHANNEL_MODE_COUNT - 1);
}

String micModeLabel() {
  return String(MIC_CHANNEL_LABELS[currentMicModeIndex()]);
}

void saveAmpPinMode() {
  ampPinMode = currentAmpPinIndex();
  prefs.putInt("ampPinMode", ampPinMode);
}

void saveMicMode() {
  micChannelMode = currentMicModeIndex();
  prefs.putInt("micMode", micChannelMode);
}

void cycleMicMode() {
  micChannelMode = (currentMicModeIndex() + 1) % MIC_CHANNEL_MODE_COUNT;
  saveMicMode();
  configureMicInput();
  lastStatus = "Mic mode " + micModeLabel();
  appendSdLog("mic_mode", micModeLabel());
}

void clampSetting(String& value, int maxLen) {
  value.trim();
  if ((int)value.length() > maxLen) value = value.substring(0, maxLen);
}

String normalizedHubBase(String input) {
  input.trim();
  if ((int)input.length() > 96) input = input.substring(0, 96);
  if (!input.startsWith("http://") && !input.startsWith("https://")) input = "http://" + input;
  while (input.endsWith("/")) input.remove(input.length() - 1);
  int schemeEnd = input.indexOf("://");
  int hostStart = schemeEnd >= 0 ? schemeEnd + 3 : 0;
  int slash = input.indexOf('/', hostStart);
  if (slash >= 0) input = input.substring(0, slash);
  int colon = input.indexOf(':', hostStart);
  if (colon >= 0) input = input.substring(0, colon);
  return input;
}

String portFromHubUrl(String input, const String& fallback) {
  input.trim();
  int schemeEnd = input.indexOf("://");
  int hostStart = schemeEnd >= 0 ? schemeEnd + 3 : 0;
  int slash = input.indexOf('/', hostStart);
  String host = slash >= 0 ? input.substring(hostStart, slash) : input.substring(hostStart);
  int colon = host.lastIndexOf(':');
  if (colon >= 0 && colon < (int)host.length() - 1) return host.substring(colon + 1);
  return fallback;
}

void updateHubUrl() {
  hubBase = normalizedHubBase(hubBase.length() ? hubBase : WISP_HUB_URL);
  hubPort.trim();
  clampSetting(hubPort, 6);
  hubUrl = hubBase;
  if (hubPort.length()) hubUrl += ":" + hubPort;
}

void loadNetworkSettings() {
  wifiSsid = prefs.getString("wifiSsid", WISP_WIFI_SSID);
  wifiPassword = prefs.getString("wifiPass", WISP_WIFI_PASSWORD);
  hubBase = prefs.getString("hubBase", normalizedHubBase(WISP_HUB_URL));
  hubPort = prefs.getString("hubPort", portFromHubUrl(WISP_HUB_URL, ""));
  defaultWorkflow = prefs.getString("workflow", WISP_DEFAULT_WORKFLOW);
  clampSetting(wifiSsid, 32);
  clampSetting(wifiPassword, 64);
  clampSetting(hubBase, 96);
  clampSetting(hubPort, 6);
  clampSetting(defaultWorkflow, 80);
  updateHubUrl();
}

void saveNetworkSettings() {
  clampSetting(wifiSsid, 32);
  clampSetting(wifiPassword, 64);
  clampSetting(hubBase, 96);
  clampSetting(hubPort, 6);
  updateHubUrl();
  prefs.putString("wifiSsid", wifiSsid);
  prefs.putString("wifiPass", wifiPassword);
  prefs.putString("hubBase", hubBase);
  prefs.putString("hubPort", hubPort);
  lastStatus = "Settings saved.";
}

void loadCompiledDefaults() {
  wifiSsid = WISP_WIFI_SSID;
  wifiPassword = WISP_WIFI_PASSWORD;
  hubBase = normalizedHubBase(WISP_HUB_URL);
  hubPort = portFromHubUrl(WISP_HUB_URL, "");
  updateHubUrl();
  lastStatus = "Defaults loaded. Tap Connect.";
}

String wifiStatusName(wl_status_t status) {
  if (status == WL_CONNECTED) return "connected";
  if (status == WL_NO_SSID_AVAIL) return "SSID not found";
  if (status == WL_CONNECT_FAILED) return "bad password";
  if (status == WL_CONNECTION_LOST) return "signal lost";
  if (status == WL_DISCONNECTED) return "disconnected";
  if (status == WL_IDLE_STATUS) return "idle";
  return "status " + String((int)status);
}

String wifiPowerName(wifi_power_t power) {
  if (power == WIFI_POWER_19_5dBm) return "19.5dBm";
  if (power == WIFI_POWER_19dBm) return "19dBm";
  if (power == WIFI_POWER_18_5dBm) return "18.5dBm";
  if (power == WIFI_POWER_17dBm) return "17dBm";
  if (power == WIFI_POWER_15dBm) return "15dBm";
  if (power == WIFI_POWER_13dBm) return "13dBm";
  if (power == WIFI_POWER_11dBm) return "11dBm";
  if (power == WIFI_POWER_8_5dBm) return "8.5dBm";
  if (power == WIFI_POWER_7dBm) return "7dBm";
  if (power == WIFI_POWER_5dBm) return "5dBm";
  if (power == WIFI_POWER_2dBm) return "2dBm";
  return "custom";
}

void drawHeader(const String& title, uint16_t accent = C_BLUE) {
  tft.fillScreen(C_BG);
  tft.fillRoundRect(6, 6, SCREEN_W - 12, 36, 8, accent);
  tft.setTextColor(ILI9341_BLACK, accent);
  tft.setTextDatum(ML_DATUM);
  tft.drawString(title, 16, 24, 4);
  tft.setTextDatum(MR_DATUM);
  tft.drawString(WiFi.isConnected() ? "Wi-Fi" : "Offline", SCREEN_W - 16, 24, 2);
  tft.setTextDatum(TL_DATUM);
}

void drawButton(const Button& b) {
  tft.fillRoundRect(b.x, b.y, b.w, b.h, 8, b.color);
  tft.drawRoundRect(b.x, b.y, b.w, b.h, 8, ILI9341_WHITE);
  tft.setTextDatum(MC_DATUM);
  tft.setTextColor(readableTextColor(b.color), b.color);
  tft.drawString(b.label, b.x + b.w / 2, b.y + b.h / 2, 2);
  tft.setTextDatum(TL_DATUM);
}

void drawTabs() {
  Button tabs[] = {
    {4, 204, 49, 30, "Stat", currentScreen == SCREEN_STATUS ? C_BLUE : C_PANEL},
    {56, 204, 49, 30, "Pair", currentScreen == SCREEN_PAIR ? C_BLUE : C_PANEL},
    {108, 204, 49, 30, "Cmd", currentScreen == SCREEN_COMMANDS ? C_BLUE : C_PANEL},
    {160, 204, 49, 30, "Demo", currentScreen == SCREEN_DEMO ? C_BLUE : C_PANEL},
    {212, 204, 49, 30, "Mic", currentScreen == SCREEN_MIC ? C_BLUE : C_PANEL},
    {264, 204, 52, 30, "Set", currentScreen == SCREEN_SETUP ? C_BLUE : C_PANEL},
  };
  for (auto& tab : tabs) drawButton(tab);
}

void drawBusyOverlay(const String& label) {
  tft.fillRoundRect(76, 92, 168, 48, 10, C_PANEL);
  tft.drawRoundRect(76, 92, 168, 48, 10, C_BLUE);
  tft.setTextDatum(MC_DATUM);
  tft.setTextColor(ILI9341_WHITE, C_PANEL);
  tft.drawString(label, 160, 116, 2);
  tft.setTextDatum(TL_DATUM);
}

void drawWrapped(const String& text, int x, int y, int maxChars, int maxLines, uint16_t color = ILI9341_WHITE) {
  tft.setTextColor(color, C_BG);
  int line = 0;
  int start = 0;
  while (start < (int)text.length() && line < maxLines) {
    int end = min(start + maxChars, (int)text.length());
    int breakAt = end;
    for (int i = end; i > start + 8; --i) {
      if (text[i] == ' ') {
        breakAt = i;
        break;
      }
    }
    tft.drawString(text.substring(start, breakAt), x, y + line * 18, 2);
    start = breakAt;
    while (start < (int)text.length() && text[start] == ' ') start++;
    line++;
  }
}

void drawSplash(const String& status) {
  tft.fillScreen(C_BG);
  for (int y = 0; y < SCREEN_H; y += 6) {
    uint8_t blue = 18 + min(42, y / 4);
    uint16_t color = ((0 & 0xF8) << 8) | ((8 & 0xFC) << 3) | (blue >> 3);
    tft.fillRect(0, y, SCREEN_W, 6, color);
  }
  tft.fillRoundRect(12, 12, SCREEN_W - 24, SCREEN_H - 24, 18, 0x0863);
  tft.drawRoundRect(12, 12, SCREEN_W - 24, SCREEN_H - 24, 18, C_BLUE);
  tft.drawRGBBitmap(24, 44, WISP_MASCOT, WISP_MASCOT_W, WISP_MASCOT_H);
  tft.setTextDatum(TL_DATUM);
  tft.setTextColor(ILI9341_WHITE, 0x0863);
  tft.drawString("NodeSpark", 160, 54, 4);
  tft.setTextColor(C_BLUE, 0x0863);
  tft.drawString("Wisp Touch", 160, 82, 4);
  tft.setTextColor(C_MUTED, 0x0863);
  drawWrapped(status, 160, 122, 20, 3, C_MUTED);
  tft.fillRoundRect(160, 184, 126, 24, 8, C_PANEL);
  tft.setTextDatum(MC_DATUM);
  tft.setTextColor(C_PINK, C_PANEL);
  tft.drawString("NodeSparkHub", 223, 196, 2);
  tft.setTextDatum(TL_DATUM);
}

void drawStatus() {
  drawHeader("NodeSpark Wisp ESP32", C_BLUE);
  tft.fillRoundRect(12, 54, 142, 54, 8, C_PANEL);
  tft.fillRoundRect(166, 54, 142, 54, 8, C_PANEL);
  tft.fillRoundRect(12, 118, 296, 54, 8, C_PANEL);

  tft.setTextColor(C_MUTED, C_PANEL);
  tft.drawString("Network", 24, 64, 2);
  tft.drawString("Hub", 178, 64, 2);
  tft.drawString("Status", 24, 128, 2);

  tft.setTextColor(WiFi.isConnected() ? C_GREEN : C_AMBER, C_PANEL);
  tft.drawString(WiFi.isConnected() ? WiFi.localIP().toString() : (wifiConnectState == WIFI_IDLE ? "Offline" : "Connecting"), 24, 82, 2);
  tft.setTextColor(token.length() ? C_GREEN : C_AMBER, C_PANEL);
  tft.drawString(token.length() ? "Paired" : "Pair needed", 178, 82, 2);
  tft.setTextColor(C_MUTED, C_PANEL);
  tft.drawString(clipped(lastStatus, 34), 24, 146, 2);
  tft.setTextColor(lastSdStatus.startsWith("SD OK") ? C_GREEN : C_MUTED, C_PANEL);
  tft.drawString(clipped(lastSdStatus, 32), 24, 164, 2);
  drawTabs();
}

void drawPair() {
  drawHeader("Pair With NodeSparkHub", C_PINK);
  tft.setTextColor(ILI9341_WHITE, C_BG);
  tft.drawString("Enter Hub pairing code:", 14, 52, 2);
  tft.fillRoundRect(14, 75, 140, 32, 8, C_PANEL);
  tft.setTextDatum(MC_DATUM);
  tft.setTextColor(C_GREEN, C_PANEL);
  tft.drawString(pairCode.length() ? pairCode : "------", 84, 91, 4);
  tft.setTextDatum(TL_DATUM);

  int n = 1;
  for (int row = 0; row < 3; row++) {
    for (int col = 0; col < 3; col++) {
      drawButton({170 + col * 46, 52 + row * 38, 38, 30, String(n++), C_BLUE});
    }
  }
  drawButton({216, 166, 38, 30, "0", C_BLUE});
  drawButton({14, 116, 68, 34, "Pair", C_GREEN});
  drawButton({88, 116, 66, 34, "Clear", C_AMBER});
  drawWrapped("Create a pairing code in NodeSparkHub: Settings > Hub Server > Devices.", 14, 158, 34, 2, C_MUTED);
  drawTabs();
}

void drawCommands() {
  drawHeader("Hub Commands", C_GREEN);
  if (pendingApprovalId.length()) {
    tft.setTextColor(C_AMBER, C_BG);
    tft.drawString("Approval needed", 14, 52, 4);
    drawWrapped(pendingApprovalTitle + ": " + pendingApprovalBody, 14, 84, 35, 3, ILI9341_WHITE);
    drawButton({24, 150, 120, 36, "Approve", C_GREEN});
    drawButton({176, 150, 120, 36, "Reject", C_RED});
  } else {
    tft.setTextColor(ILI9341_WHITE, C_BG);
    tft.drawString("Last command", 14, 56, 2);
    drawWrapped(lastCommand, 14, 84, 36, 5, C_MUTED);
  }
  drawTabs();
}

void drawDemo() {
  drawHeader("Touch Demo Console", C_BLUE);
  drawButton({14, 56, 136, 42, "Ping Hub", C_GREEN});
  drawButton({170, 56, 136, 42, "Ask AI", C_PINK});
  drawButton({14, 112, 136, 42, "Workflow", C_AMBER});
  drawButton({170, 112, 136, 42, "Chime", C_PINK});
  drawWrapped("Ask AI runs the Wisp Assistant workflow on NodeSparkHub and shows the response here.", 14, 166, 38, 2, C_MUTED);
  drawTabs();
}

void drawMicMeter(int level) {
  int clamped = constrain(level, 0, 1023);
  int barW = map(clamped, 0, 1023, 2, 284);
  uint16_t color = clamped > 520 ? C_GREEN : (clamped > 120 ? C_AMBER : C_BLUE);

  tft.fillRoundRect(16, 52, 288, 76, 10, C_PANEL);
  tft.drawRoundRect(16, 52, 288, 76, 10, color);
  tft.setTextColor(C_MUTED, C_PANEL);
  tft.drawString("Live mic input", 28, 62, 2);
  tft.setTextColor(color, C_PANEL);
  tft.drawString(String(clamped), 240, 58, 4);
  tft.setTextColor(lastMicBytes > 0 ? C_GREEN : C_AMBER, C_PANEL);
  tft.drawString("bytes " + String(lastMicBytes), 28, 84, 2);
  tft.fillRoundRect(28, 108, 264, 10, 5, 0x2945);
  tft.fillRoundRect(28, 108, min(barW, 264), 10, 5, color);
}

void drawMic() {
  drawHeader("INMP441 Mic", C_PINK);
  tft.setTextColor(ILI9341_WHITE, C_BG);
  String status = "Amp ";
  status += ampReady ? "ready" : "off";
  status += "   Mic ";
  status += micReady ? "ready" : "off";
  status += " ";
  status += micModeLabel();
  if (ampMuted) status += "   muted";
  tft.setTextColor(C_MUTED, C_BG);
  tft.drawString(clipped(status + "  Vol " + String(audioVolumePercent) + "%  " + ampPinModeLabel(), 38), 16, 130, 2);
  drawMicMeter(lastMicLevel);
  drawButton({14, 146, 54, 28, "Vol-", C_PANEL});
  drawButton({76, 146, 54, 28, "Vol+", C_PANEL});
  drawButton({138, 146, 54, 28, "Pins", C_AMBER});
  drawButton({200, 146, 48, 28, "Tone", C_PINK});
  drawButton({256, 146, 50, 28, "Mode", C_BLUE});
  drawButton({14, 178, 84, 24, ampMuted ? "Unmute" : "Mute", C_PANEL});
  drawButton({108, 178, 198, 24, "Voice Run", C_AMBER});
  drawTabs();
}

String inputTitle() {
  if (inputTarget == INPUT_SSID) return "Wi-Fi Name";
  if (inputTarget == INPUT_WIFI_PASSWORD) return "Wi-Fi Password";
  if (inputTarget == INPUT_HUB_BASE) return "Hub URL";
  if (inputTarget == INPUT_HUB_PORT) return "Hub Port";
  return "Input";
}

void drawSettingsMain() {
  Serial.println("[ui] draw setup");
  drawHeader("Wisp Setup", C_AMBER);
  drawButton({6, 48, 58, 28, "Scan", C_BLUE});
  drawButton({68, 48, 58, 28, "Def", C_PANEL});
  drawButton({130, 48, 58, 28, "Conn", C_GREEN});
  drawButton({192, 48, 58, 28, "Save", C_PINK});
  drawButton({254, 48, 58, 28, "SD", C_AMBER});

  tft.setTextColor(C_MUTED, C_BG);
  tft.drawString("SSID", 14, 86, 2);
  tft.fillRoundRect(74, 82, 232, 24, 6, C_PANEL);
  tft.setTextColor(ILI9341_WHITE, C_PANEL);
  tft.drawString(wifiSsid.length() ? clipped(wifiSsid, 24) : "tap or scan", 82, 88, 2);

  tft.setTextColor(C_MUTED, C_BG);
  tft.drawString("Pass", 14, 114, 2);
  tft.fillRoundRect(74, 110, 232, 24, 6, C_PANEL);
  tft.setTextColor(ILI9341_WHITE, C_PANEL);
  tft.drawString(wifiPassword.length() ? repeatedChar('*', min(14, (int)wifiPassword.length())) : "tap to enter", 82, 116, 2);

  tft.setTextColor(C_MUTED, C_BG);
  tft.drawString("URL", 14, 142, 2);
  tft.fillRoundRect(74, 138, 232, 24, 6, C_PANEL);
  tft.setTextColor(ILI9341_WHITE, C_PANEL);
  tft.drawString(clipped(hubBase, 25), 82, 144, 2);

  tft.setTextColor(C_MUTED, C_BG);
  tft.drawString("Port", 14, 170, 2);
  tft.fillRoundRect(74, 166, 90, 22, 6, C_PANEL);
  tft.setTextColor(ILI9341_WHITE, C_PANEL);
  tft.drawString(hubPort.length() ? clipped(hubPort, 6) : "none", 82, 172, 2);
  tft.setTextColor(WiFi.isConnected() ? C_GREEN : C_AMBER, C_BG);
  tft.drawString(clipped(lastStatus, 16), 174, 166, 2);
  tft.setTextColor(lastSdStatus.startsWith("SD OK") ? C_GREEN : C_MUTED, C_BG);
  tft.drawString(clipped(lastSdStatus, 28), 174, 184, 2);
  drawTabs();
}

void drawWifiList() {
  drawHeader("Choose Wi-Fi", C_BLUE);
  drawButton({12, 50, 90, 28, "Rescan", C_BLUE});
  drawButton({218, 50, 90, 28, "Back", C_AMBER});
  if (!scannedCount) {
    drawWrapped(lastStatus.startsWith("Scan failed") ? lastStatus : "No networks found. Tap Rescan or enter SSID manually from Setup.", 14, 96, 36, 3, C_MUTED);
  }
  for (int i = 0; i < scannedCount; i++) {
    int y = 84 + i * 19;
    tft.fillRoundRect(14, y, 292, 17, 5, scannedSsids[i] == wifiSsid ? C_BLUE : C_PANEL);
    tft.setTextColor(scannedSsids[i] == wifiSsid ? ILI9341_BLACK : ILI9341_WHITE, scannedSsids[i] == wifiSsid ? C_BLUE : C_PANEL);
    String row = scannedSsids[i].substring(0, 23) + " " + String(scannedRssi[i]) + "dBm";
    tft.drawString(row, 22, y + 3, 2);
  }
  drawTabs();
}

void drawKeyboard() {
  drawHeader(inputTitle(), C_PINK);
  tft.fillRoundRect(8, 48, 304, 28, 6, C_PANEL);
  tft.setTextColor(ILI9341_WHITE, C_PANEL);
  String shown = editBuffer;
  if (!shown.length()) shown = inputTarget == INPUT_HUB_BASE ? "http:// or https://" : "";
  drawWrapped(shown, 16, 56, 34, 1, ILI9341_WHITE);

  const char* rows0[] = {"qwertyuiop", "asdfghjkl@", "zxcvbnm.-_"};
  const char* rows1[] = {"QWERTYUIOP", "ASDFGHJKL@", "ZXCVBNM.-_"};
  const char* rows2[] = {"1234567890", ":/?.#&+=!", "[]{}()%*$'"};
  const char** rows = keyboardPage == 0 ? rows0 : (keyboardPage == 1 ? rows1 : rows2);
  for (int r = 0; r < 3; r++) {
    int y = 84 + r * 28;
    for (int c = 0; c < 10; c++) {
      int x = 4 + c * 31;
      tft.fillRoundRect(x, y, 28, 23, 5, C_PANEL);
      tft.setTextDatum(MC_DATUM);
      tft.setTextColor(ILI9341_WHITE, C_PANEL);
      tft.drawString(String(rows[r][c]), x + 14, y + 12, 2);
      tft.setTextDatum(TL_DATUM);
    }
  }
  drawButton({4, 170, 50, 28, "Del", C_AMBER});
  drawButton({58, 170, 74, 28, "Space", C_BLUE});
  drawButton({136, 170, 54, 28, keyboardPage == 0 ? "ABC" : (keyboardPage == 1 ? "123" : "abc"), C_PANEL});
  drawButton({194, 170, 54, 28, "Clear", C_RED});
  drawButton({252, 170, 64, 28, "Done", C_GREEN});
}

void drawSetup() {
  if (setupView == SETUP_WIFI_LIST) drawWifiList();
  else if (setupView == SETUP_INPUT) drawKeyboard();
  else drawSettingsMain();
}

void redraw() {
  if (currentScreen == SCREEN_STATUS) drawStatus();
  else if (currentScreen == SCREEN_PAIR) drawPair();
  else if (currentScreen == SCREEN_COMMANDS) drawCommands();
  else if (currentScreen == SCREEN_DEMO) drawDemo();
  else if (currentScreen == SCREEN_MIC) drawMic();
  else drawSetup();
}

bool touched(int& x, int& y) {
  digitalWrite(PIN_TFT_CS, HIGH);
  digitalWrite(PIN_TOUCH_CS, HIGH);
#if WISP_ENABLE_SD
  digitalWrite(PIN_SD_CS, HIGH);
#endif
  delayMicroseconds(8);
  if (!touch.touched()) {
    digitalWrite(PIN_TOUCH_CS, HIGH);
    digitalWrite(PIN_TFT_CS, HIGH);
#if WISP_ENABLE_SD
    digitalWrite(PIN_SD_CS, HIGH);
#endif
    return false;
  }
  TS_Point p = touch.getPoint();
  digitalWrite(PIN_TOUCH_CS, HIGH);
  digitalWrite(PIN_TFT_CS, HIGH);
#if WISP_ENABLE_SD
  digitalWrite(PIN_SD_CS, HIGH);
#endif
  x = map(p.x, WISP_TOUCH_MIN_X, WISP_TOUCH_MAX_X, 0, SCREEN_W);
  y = map(p.y, WISP_TOUCH_MIN_Y, WISP_TOUCH_MAX_Y, 0, SCREEN_H);
  x = constrain(x, 0, SCREEN_W - 1);
  y = constrain(y, 0, SCREEN_H - 1);
  return true;
}

bool inBox(int x, int y, const Button& b) {
  return x >= b.x && x < b.x + b.w && y >= b.y && y < b.y + b.h;
}

void beginInput(InputTarget target, const String& value) {
  inputTarget = target;
  editBuffer = value;
  keyboardPage = target == INPUT_HUB_PORT ? 2 : 0;
  setupView = SETUP_INPUT;
  currentScreen = SCREEN_SETUP;
  drawKeyboard();
}

bool connectWifiAttempt(const String& ssid, const String& password, bool splash, uint32_t timeoutMs, const String& label) {
  String targetSsid = ssid;
  targetSsid.trim();
  if (!targetSsid.length()) {
    lastStatus = "Choose Wi-Fi in Setup.";
    if (!splash) drawSetup();
    return false;
  }
  Serial.printf("[wifi] connecting to %s with power %s\n", targetSsid.c_str(), wifiPowerName(WISP_WIFI_TX_POWER).c_str());
  WiFi.persistent(false);
  WiFi.disconnect(true, true);
  WiFi.scanDelete();
  delay(250);
  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);
  WiFi.setHostname("nodespark-wisp");
  WiFi.setTxPower(WISP_WIFI_TX_POWER);
  delay(150);
  WiFi.begin(targetSsid.c_str(), password.c_str());
  lastStatus = label.length() ? label : "Connecting Wi-Fi...";
  if (splash) drawSplash(lastStatus);
  else drawSettingsMain();
  uint32_t start = millis();
  uint32_t lastUiMs = 0;
  while (WiFi.status() != WL_CONNECTED && millis() - start < timeoutMs) {
    delay(250);
    if (!splash && millis() - lastUiMs > 1500) {
      lastUiMs = millis();
      lastStatus = "Wi-Fi " + String((millis() - start) / 1000) + "s " + wifiStatusName(WiFi.status());
      drawSettingsMain();
    }
    yield();
  }
  lastStatus = WiFi.isConnected()
    ? "Wi-Fi " + WiFi.localIP().toString()
    : "Wi-Fi " + wifiStatusName(WiFi.status());
  Serial.printf("[wifi] %s\n", lastStatus.c_str());
  return WiFi.isConnected();
}

bool connectWifi(bool splash, uint32_t timeoutMs = WISP_WIFI_CONNECT_TIMEOUT_MS) {
  wifiSsid.trim();
  bool connected = connectWifiAttempt(wifiSsid, wifiPassword, splash, timeoutMs, "Connecting saved Wi-Fi...");
  if (connected) return true;

  String compiledSsid = WISP_WIFI_SSID;
  String compiledPassword = WISP_WIFI_PASSWORD;
  compiledSsid.trim();
  if (!compiledSsid.length()) return false;
  if (compiledSsid == wifiSsid && compiledPassword == wifiPassword) return false;

  lastStatus = "Saved Wi-Fi failed. Trying defaults...";
  if (!splash) drawSettingsMain();
  connected = connectWifiAttempt(compiledSsid, compiledPassword, splash, timeoutMs, "Connecting default Wi-Fi...");
  if (connected) {
    wifiSsid = compiledSsid;
    wifiPassword = compiledPassword;
    saveNetworkSettings();
  }
  return connected;
}

void redrawWifiStatusIfVisible() {
  if (currentScreen == SCREEN_SETUP && setupView == SETUP_MAIN) drawSettingsMain();
  else if (currentScreen == SCREEN_STATUS) drawStatus();
}

void beginWifiAttempt(const String& ssid, const String& password, WifiConnectState state) {
  activeWifiSsid = ssid;
  activeWifiPassword = password;
  activeWifiSsid.trim();
  if (!activeWifiSsid.length()) {
    wifiConnectState = WIFI_IDLE;
    lastStatus = "Choose Wi-Fi in Setup.";
    redrawWifiStatusIfVisible();
    return;
  }

  wifiConnectState = state;
  wifiConnectStartMs = millis();
  lastWifiStatusMs = 0;
  lastStatus = state == WIFI_CONNECTING_DEFAULT ? "Auto Wi-Fi defaults..." : "Auto Wi-Fi saved...";
  Serial.printf("[wifi] async connect to %s with power %s\n", activeWifiSsid.c_str(), wifiPowerName(WISP_WIFI_TX_POWER).c_str());

  WiFi.persistent(false);
  WiFi.disconnect(true, true);
  WiFi.scanDelete();
  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);
  WiFi.setHostname("nodespark-wisp");
  WiFi.setTxPower(WISP_WIFI_TX_POWER);
  WiFi.begin(activeWifiSsid.c_str(), activeWifiPassword.c_str());
  redrawWifiStatusIfVisible();
}

void startWifiConnect(bool useSavedFirst = true) {
  String compiledSsid = WISP_WIFI_SSID;
  String compiledPassword = WISP_WIFI_PASSWORD;
  compiledSsid.trim();
  wifiSsid.trim();

  if (useSavedFirst && wifiSsid.length()) {
    beginWifiAttempt(wifiSsid, wifiPassword, WIFI_CONNECTING_SAVED);
  } else if (compiledSsid.length()) {
    beginWifiAttempt(compiledSsid, compiledPassword, WIFI_CONNECTING_DEFAULT);
  } else {
    wifiConnectState = WIFI_IDLE;
    lastStatus = "Choose Wi-Fi in Setup.";
    redrawWifiStatusIfVisible();
  }
}

void serviceWifiConnect() {
  if (wifiConnectState == WIFI_IDLE) return;

  if (WiFi.isConnected()) {
    lastStatus = "Wi-Fi " + WiFi.localIP().toString();
    Serial.printf("[wifi] connected %s\n", lastStatus.c_str());
    appendSdLog("wifi_connected", WiFi.localIP().toString());
    if (wifiConnectState == WIFI_CONNECTING_DEFAULT) {
      wifiSsid = activeWifiSsid;
      wifiPassword = activeWifiPassword;
      saveNetworkSettings();
    }
    wifiConnectState = WIFI_IDLE;
    lastCheckinMs = 0;
    redrawWifiStatusIfVisible();
    return;
  }

  uint32_t now = millis();
  if (now - wifiConnectStartMs > WISP_WIFI_CONNECT_TIMEOUT_MS) {
    String failedState = wifiStatusName(WiFi.status());
    Serial.printf("[wifi] async failed: %s\n", failedState.c_str());
    if (wifiConnectState == WIFI_CONNECTING_SAVED) {
      String compiledSsid = WISP_WIFI_SSID;
      String compiledPassword = WISP_WIFI_PASSWORD;
      compiledSsid.trim();
      if (compiledSsid.length() && (compiledSsid != activeWifiSsid || compiledPassword != activeWifiPassword)) {
        lastStatus = "Saved failed. Trying defaults...";
        beginWifiAttempt(compiledSsid, compiledPassword, WIFI_CONNECTING_DEFAULT);
        return;
      }
    }
    wifiConnectState = WIFI_IDLE;
    lastStatus = "Wi-Fi " + failedState;
    appendSdLog("wifi_failed", failedState);
    redrawWifiStatusIfVisible();
    return;
  }

  if (now - lastWifiStatusMs > 1500) {
    lastWifiStatusMs = now;
    lastStatus = "Wi-Fi " + String((now - wifiConnectStartMs) / 1000) + "s " + wifiStatusName(WiFi.status());
    redrawWifiStatusIfVisible();
  }
}

void scanWifiNetworks() {
  setupView = SETUP_WIFI_LIST;
  drawHeader("Choose Wi-Fi", C_BLUE);
  drawWrapped("Scanning nearby networks...", 16, 72, 34, 2, C_MUTED);
  WiFi.persistent(false);
  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);
  WiFi.setTxPower(WISP_WIFI_TX_POWER);
  WiFi.disconnect(false);
  WiFi.scanDelete();
  delay(350);
  int found = WiFi.scanNetworks(false, true, false, 450);
  if (found < 0) {
    scannedCount = 0;
    lastStatus = "Scan failed " + String(found);
    drawWifiList();
    return;
  }
  scannedCount = constrain(found, 0, 6);
  for (int i = 0; i < scannedCount; i++) {
    scannedSsids[i] = WiFi.SSID(i);
    scannedRssi[i] = WiFi.RSSI(i);
    yield();
  }
  lastStatus = "Found " + String(scannedCount) + " networks";
  drawWifiList();
}

void finishInput() {
  if (inputTarget == INPUT_SSID) wifiSsid = editBuffer;
  else if (inputTarget == INPUT_WIFI_PASSWORD) wifiPassword = editBuffer;
  else if (inputTarget == INPUT_HUB_BASE) hubBase = normalizedHubBase(editBuffer);
  else if (inputTarget == INPUT_HUB_PORT) {
    hubPort = "";
    for (size_t i = 0; i < editBuffer.length(); i++) {
      if (isDigit(editBuffer[i])) hubPort += editBuffer[i];
    }
  }
  updateHubUrl();
  inputTarget = INPUT_NONE;
  setupView = SETUP_MAIN;
  drawSettingsMain();
}

char keyboardCharAt(int x, int y) {
  const char* rows0[] = {"qwertyuiop", "asdfghjkl@", "zxcvbnm.-_"};
  const char* rows1[] = {"QWERTYUIOP", "ASDFGHJKL@", "ZXCVBNM.-_"};
  const char* rows2[] = {"1234567890", ":/?.#&+=!", "[]{}()%*$'"};
  const char** rows = keyboardPage == 0 ? rows0 : (keyboardPage == 1 ? rows1 : rows2);
  for (int r = 0; r < 3; r++) {
    int rowY = 84 + r * 28;
    if (y < rowY || y >= rowY + 23) continue;
    int c = (x - 4) / 31;
    int keyX = 4 + c * 31;
    if (c >= 0 && c < 10 && x >= keyX && x < keyX + 28) return rows[r][c];
  }
  return 0;
}

void handleKeyboardTouch(int x, int y) {
  char c = keyboardCharAt(x, y);
  if (c) {
    if (inputTarget != INPUT_HUB_PORT || isDigit(c)) {
      if (editBuffer.length() < 63) editBuffer += c;
    }
    drawKeyboard();
    return;
  }
  if (inBox(x, y, {4, 170, 50, 28, "", C_AMBER})) {
    if (editBuffer.length()) editBuffer.remove(editBuffer.length() - 1);
    drawKeyboard();
  } else if (inBox(x, y, {58, 170, 74, 28, "", C_BLUE})) {
    if (inputTarget != INPUT_HUB_PORT && editBuffer.length() < 63) editBuffer += ' ';
    drawKeyboard();
  } else if (inBox(x, y, {136, 170, 54, 28, "", C_PANEL})) {
    keyboardPage = (keyboardPage + 1) % 3;
    drawKeyboard();
  } else if (inBox(x, y, {194, 170, 54, 28, "", C_RED})) {
    editBuffer = "";
    drawKeyboard();
  } else if (inBox(x, y, {252, 170, 64, 28, "", C_GREEN})) {
    finishInput();
  }
}

void runGuardedAction(const String& label, void (*action)()) {
  if (actionBusy) return;
  actionBusy = true;
  drawBusyOverlay(label);
  yield();
  action();
  yield();
  actionBusy = false;
}

void actionScanWifi() { scanWifiNetworks(); }
void actionConnectWifi() {
  saveNetworkSettings();
  startWifiConnect(true);
}
void actionLoadDefaults() {
  loadCompiledDefaults();
  saveNetworkSettings();
  drawSettingsMain();
}
void actionSaveSettings() {
  saveNetworkSettings();
  drawSettingsMain();
}

void actionCheckSdCard() {
#if !WISP_ENABLE_SD
  lastSdStatus = "SD disabled";
  drawHeader("SD Card Check", C_AMBER);
  drawWrapped("SD support is disabled in this firmware build.", 18, 70, 34, 3, ILI9341_WHITE);
  drawTabs();
  return;
#else
  Serial.println("[sd] checking card");
  digitalWrite(PIN_TFT_CS, HIGH);
  digitalWrite(PIN_TOUCH_CS, HIGH);
  digitalWrite(PIN_SD_CS, HIGH);
  delay(20);

  bool mounted = SD.begin(PIN_SD_CS, SPI, 4000000);
  if (!mounted) {
    lastSdStatus = "SD mount failed";
    Serial.println("[sd] mount failed");
    drawHeader("SD Card Check", C_RED);
    drawWrapped("No SD card found. Check 3V3/5V, GND, MISO, MOSI, SCK, and CS GPIO14.", 18, 62, 34, 4, ILI9341_WHITE);
    drawTabs();
    digitalWrite(PIN_SD_CS, HIGH);
    return;
  }

  uint8_t type = SD.cardType();
  uint64_t size = SD.cardSize();
  const char* testPath = "/nodespark_wisp_test.txt";
  bool writeOk = false;
  bool readOk = false;

  File file = SD.open(testPath, FILE_WRITE);
  if (file) {
    writeOk = file.println("NodeSpark Wisp SD OK") > 0;
    file.close();
  }
  file = SD.open(testPath, FILE_READ);
  if (file) {
    String content = file.readStringUntil('\n');
    content.trim();
    readOk = content == "NodeSpark Wisp SD OK";
    file.close();
  }

  String typeName = "Unknown";
  if (type == CARD_MMC) typeName = "MMC";
  else if (type == CARD_SD) typeName = "SDSC";
  else if (type == CARD_SDHC) typeName = "SDHC";
  else if (type == CARD_NONE) typeName = "None";

  bool ok = type != CARD_NONE && writeOk && readOk;
  lastSdStatus = ok ? "SD OK " + formatBytes(size) : "SD test failed";
  Serial.printf("[sd] type=%s size=%s write=%s read=%s\n", typeName.c_str(), formatBytes(size).c_str(), writeOk ? "ok" : "fail", readOk ? "ok" : "fail");

  drawHeader("SD Card Check", ok ? C_GREEN : C_RED);
  String body = ok
    ? "SD card ready. Type " + typeName + ", size " + formatBytes(size) + ". Write/read test passed."
    : "SD mounted, but write/read test failed. Check card format, module power, and CS GPIO14.";
  drawWrapped(body, 18, 62, 34, 5, ILI9341_WHITE);
  drawTabs();
  if (file = SD.open("/nodespark_wisp_log.txt", FILE_APPEND)) {
    file.print(millis());
    file.print(",sd_check,");
    file.print(ok ? "ok" : "failed");
    file.print(" ");
    file.println(formatBytes(size));
    file.close();
  }
  SD.end();
  digitalWrite(PIN_SD_CS, HIGH);
#endif
}

void handleSetupTouch(int x, int y) {
  if (setupView == SETUP_INPUT) {
    handleKeyboardTouch(x, y);
    return;
  }
  if (setupView == SETUP_WIFI_LIST) {
    if (inBox(x, y, {12, 50, 90, 28, "", C_BLUE})) {
      runGuardedAction("Scanning...", actionScanWifi);
      return;
    }
    if (inBox(x, y, {218, 50, 90, 28, "", C_AMBER})) {
      setupView = SETUP_MAIN;
      drawSettingsMain();
      return;
    }
    for (int i = 0; i < scannedCount; i++) {
      if (inBox(x, y, {14, 84 + i * 19, 292, 17, "", C_PANEL})) {
        wifiSsid = scannedSsids[i];
        setupView = SETUP_MAIN;
        drawSettingsMain();
        return;
      }
    }
    return;
  }

  if (inBox(x, y, {6, 44, 60, 38, "", C_BLUE})) runGuardedAction("Scanning...", actionScanWifi);
  else if (inBox(x, y, {66, 44, 62, 38, "", C_PANEL})) {
    runGuardedAction("Defaults...", actionLoadDefaults);
  } else if (inBox(x, y, {128, 44, 62, 38, "", C_GREEN})) {
    runGuardedAction("Connecting...", actionConnectWifi);
  }
  else if (inBox(x, y, {190, 44, 62, 38, "", C_PINK})) {
    runGuardedAction("Saving...", actionSaveSettings);
  } else if (inBox(x, y, {252, 44, 62, 38, "", C_AMBER})) {
    runGuardedAction("SD Check...", actionCheckSdCard);
  } else if (inBox(x, y, {70, 78, 240, 30, "", C_PANEL})) beginInput(INPUT_SSID, wifiSsid);
  else if (inBox(x, y, {70, 106, 240, 30, "", C_PANEL})) beginInput(INPUT_WIFI_PASSWORD, wifiPassword);
  else if (inBox(x, y, {70, 134, 240, 30, "", C_PANEL})) beginInput(INPUT_HUB_BASE, hubBase);
  else if (inBox(x, y, {70, 162, 100, 30, "", C_PANEL})) beginInput(INPUT_HUB_PORT, hubPort);
}

String request(const String& method, const String& path, const String& body = "", bool auth = true) {
  if (!WiFi.isConnected()) {
    lastStatus = "No Wi-Fi. Use Set > Conn.";
    return "";
  }
  String url = hubUrl + path;
  HTTPClient http;
  http.setTimeout(WISP_HTTP_TIMEOUT_MS);
  http.setReuse(false);
  bool started = false;
  if (url.startsWith("https://")) {
    hubSecureClient.setInsecure();
    started = http.begin(hubSecureClient, url);
  } else {
    started = http.begin(hubPlainClient, url);
  }
  if (!started) {
    lastStatus = "Bad Hub URL.";
    return "";
  }
  http.addHeader("Accept", "application/json");
  http.addHeader("Content-Type", "application/json");
  http.addHeader("User-Agent", APP_VERSION);
  http.addHeader("X-NodeSparkHub-Device-ID", deviceId);
  http.addHeader("X-NodeSparkHub-Device-Name", deviceName);
  if (auth && token.length()) {
    http.addHeader("Authorization", "Bearer " + token);
    http.addHeader("X-NodeSparkHub-Token", token);
  }
  int code = method == "GET" ? http.GET() : http.POST(body);
  String payload = code > 0 ? http.getString() : "";
  http.end();
  Serial.printf("[hub] %s %s -> %d\n", method.c_str(), url.c_str(), code);
  if (code < 200 || code >= 300) {
    lastStatus = code > 0 ? "Hub HTTP " + String(code) : "Hub offline/URL wrong";
    if (payload.length()) lastStatus += " " + payload.substring(0, 40);
    return "";
  }
  return payload;
}

void ackCommand(const String& id, const String& status, const String& result) {
  if (!id.length()) return;
  String body = "{\"status\":\"" + jsonEscape(status) + "\",\"result\":\"" + jsonEscape(result) + "\"}";
  request("POST", "/devices/" + deviceId + "/commands/" + id + "/ack", body);
}

bool pairHub() {
  if (!WiFi.isConnected()) {
    lastStatus = "No Wi-Fi. Open Set > Conn.";
    return false;
  }
  if (pairCode.length() < 4) {
    lastStatus = "Enter the pairing code first.";
    return false;
  }
  String body = "{";
  body += "\"code\":\"" + jsonEscape(pairCode) + "\",";
  body += "\"deviceId\":\"" + jsonEscape(deviceId) + "\",";
  body += "\"deviceName\":\"" + jsonEscape(deviceName) + "\",";
  body += "\"platform\":\"ESP32-S3 / NodeSpark Wisp Touch\",";
  body += "\"osVersion\":\"Arduino ESP32\",";
  body += "\"appVersion\":\"" + String(APP_VERSION) + "\"}";
  String payload = request("POST", "/pair", body, false);
  if (!payload.length()) return false;
  StaticJsonDocument<1024> doc;
  if (deserializeJson(doc, payload)) {
    lastStatus = "Pair failed: bad Hub reply.";
    return false;
  }
  token = doc["deviceToken"].as<String>();
  if (!token.length()) {
    lastStatus = "Pair failed: code rejected.";
    return false;
  }
  prefs.putString("token", token);
  prefs.putString("hubId", doc["hubId"].as<String>());
  pairCode = "";
  lastStatus = "Paired with NodeSparkHub.";
  return true;
}

void checkin() {
  String body = "{";
  body += "\"deviceId\":\"" + jsonEscape(deviceId) + "\",";
  body += "\"name\":\"" + jsonEscape(deviceName) + "\",";
  body += "\"platform\":\"ESP32-S3 / NodeSpark Wisp Touch\",";
  body += "\"osVersion\":\"Arduino ESP32\",";
  body += "\"appVersion\":\"" + String(APP_VERSION) + "\",";
  body += "\"capabilities\":[\"display\",\"touch\",\"speaker\",\"microphone\",\"approval\",\"dashboard\",\"deviceCommands\",\"run\",\"pairing\"]}";
  String payload = request("POST", "/devices/checkin", body);
  if (payload.length()) {
    lastStatus = "Hub online. Commands ready.";
    appendSdLog("hub_checkin", "ok");
  }
}

void showCard(const String& title, const String& body, uint16_t accent = C_BLUE) {
  drawHeader(title.length() ? title : "NodeSparkHub", accent);
  tft.fillRoundRect(16, 58, SCREEN_W - 32, 118, 12, C_PANEL);
  tft.drawRoundRect(16, 58, SCREEN_W - 32, 118, 12, accent);
  drawWrapped(body, 28, 78, 34, 5, ILI9341_WHITE);
  drawTabs();
}

void drawDashboard(const String& title, const String& label, const String& value, const String* items = nullptr, int itemCount = 0, uint16_t accent = C_GREEN) {
  drawHeader(title.length() ? title : "Workflow Monitor", accent);
  tft.fillRoundRect(18, 58, 284, 66, 12, C_PANEL);
  tft.setTextColor(C_MUTED, C_PANEL);
  tft.drawString(label.length() ? label : "Status", 32, 70, 2);
  tft.setTextColor(accent, C_PANEL);
  tft.drawString(value.length() ? value : "Live", 32, 88, 4);
  if (items && itemCount > 0) {
    for (int i = 0; i < min(itemCount, 3); i++) {
      int y = 136 + i * 18;
      tft.fillRoundRect(20, y, 280, 16, 5, C_PANEL);
      tft.fillCircle(30, y + 8, 3, accent);
      tft.setTextColor(ILI9341_WHITE, C_PANEL);
      tft.drawString(clipped(items[i], 31), 40, y + 2, 2);
    }
  } else {
    drawWrapped("Server online   Touch ready   Device paired", 22, 142, 36, 2, C_MUTED);
  }
  drawTabs();
}

void drawIconGrid(const String& title, const String* items, int itemCount, uint16_t accent) {
  drawHeader(title.length() ? title : "NodeSpark Powers", accent);
  const char* fallback[] = {"AI", "Hub", "Phone", "Flow", "Alert", "Done"};
  for (int i = 0; i < 6; i++) {
    int col = i % 3;
    int row = i / 3;
    int x = 14 + col * 102;
    int y = 58 + row * 62;
    String label = (items && i < itemCount && items[i].length()) ? items[i] : String(fallback[i]);
    tft.fillRoundRect(x, y, 88, 48, 10, C_PANEL);
    tft.drawRoundRect(x, y, 88, 48, 10, accent);
    tft.fillCircle(x + 24, y + 24, 13, accent);
    tft.setTextColor(readableTextColor(accent), accent);
    tft.setTextDatum(MC_DATUM);
    tft.drawString(label.substring(0, 1), x + 24, y + 24, 2);
    tft.setTextDatum(TL_DATUM);
    tft.setTextColor(ILI9341_WHITE, C_PANEL);
    tft.drawString(clipped(label, 8), x + 42, y + 18, 2);
  }
  drawTabs();
}

void pushNotification(const String& title, const String& body) {
  if (notificationCount < NOTIFICATION_LIMIT) {
    notificationCount++;
  }
  for (int i = notificationCount - 1; i > 0; i--) {
    notificationTitles[i] = notificationTitles[i - 1];
    notificationBodies[i] = notificationBodies[i - 1];
  }
  notificationTitles[0] = title;
  notificationBodies[0] = body;
}

void drawNotificationStack(uint16_t accent) {
  drawHeader("Notification Center", accent);
  tft.setTextColor(C_MUTED, C_BG);
  tft.drawString(String(notificationCount) + " saved on Wisp", 16, 52, 2);
  for (int i = 0; i < notificationCount; i++) {
    int y = 76 + i * 24;
    tft.fillRoundRect(14, y, 292, 20, 6, C_PANEL);
    tft.fillCircle(26, y + 10, 4, accent);
    tft.setTextColor(ILI9341_WHITE, C_PANEL);
    tft.drawString(clipped(notificationTitles[i], 15) + ": " + clipped(notificationBodies[i], 18), 38, y + 4, 2);
  }
  if (!notificationCount) drawWrapped("No notifications yet.", 18, 86, 32, 2, C_MUTED);
  drawTabs();
}

void drawQrText(const String& title, const String& data, const String& subtitle, uint16_t accent) {
  drawHeader(title.length() ? title : "NodeSpark QR", accent);
  tft.fillRoundRect(16, 58, 98, 98, 10, ILI9341_WHITE);
  for (int y = 0; y < 7; y++) {
    for (int x = 0; x < 7; x++) {
      bool on = (x == 0 || y == 0 || x == 6 || y == 6 || ((x * 3 + y * 5 + data.length()) % 4 == 0));
      if (on) tft.fillRect(28 + x * 10, 70 + y * 10, 7, 7, ILI9341_BLACK);
    }
  }
  tft.setTextColor(ILI9341_WHITE, C_BG);
  drawWrapped(subtitle.length() ? subtitle : "Open this link from NodeSparkHub.", 132, 62, 20, 2, ILI9341_WHITE);
  drawWrapped(data, 132, 106, 21, 4, C_MUTED);
  drawTabs();
}

void drawHealthDashboard() {
  String items[4];
  items[0] = WiFi.isConnected() ? ("WiFi: " + WiFi.localIP().toString()) : "WiFi: offline";
  items[1] = token.length() ? "Hub: paired" : "Hub: pair needed";
  items[2] = "SD: " + clipped(lastSdStatus, 20);
  items[3] = String("Audio: ") + (ampReady ? "amp " : "no amp ") + (micReady ? "mic" : "no mic");
  drawDashboard("Device Health", "Reset", resetReasonName(esp_reset_reason()), items, 4, C_GREEN);
}

void runDemoSequence(const HubCommand& command) {
  drawSplash(commandBody(command, "NodeSparkHub makes physical workflows feel alive."));
  playChime(1);
  delay(700);
  showCard("Webhook Received", "Hub caught an event and routed it to this screen.", C_BLUE);
  delay(750);
  showCard("AI Thinking", "NodeSparkHub can summarize, decide, and act from the device.", C_PINK);
  delay(750);
  drawIconGrid("Real Hardware", command.items, command.itemCount, command.rgbR >= 0 ? rgb565(command.rgbR, command.rgbG, command.rgbB) : C_BLUE);
  delay(750);
  showCard(commandTitle(command, "Workflow Done"), commandBody(command, "Display, touch, commands, approvals, and Hub actions are live."), C_GREEN);
}

void writeAmpSilence(int durationMs = 160) {
  if (!ampReady) return;
  int16_t silence[128 * 2] = {0};
  size_t written = 0;
  int chunks = max(1, durationMs / 6);
  for (int i = 0; i < chunks; i++) {
    i2s_write(I2S_NUM_0, silence, sizeof(silence), &written, pdMS_TO_TICKS(80));
    yield();
  }
  i2s_zero_dma_buffer(I2S_NUM_0);
}

void setAmpMuted(bool muted) {
  ampMuted = muted;
  if (ampMuted) {
    writeAmpSilence(220);
    lastStatus = "Amp muted.";
  } else {
    lastStatus = "Amp unmuted.";
  }
}

void playChime(int kind = 0) {
  if (!ampReady) {
    lastStatus = "Amp not ready. Check MAX98357 wiring.";
    Serial.println("[audio] chime skipped: amp not ready");
    return;
  }
  if (ampMuted) {
    lastStatus = "Amp muted.";
    writeAmpSilence(120);
    return;
  }
  const int sampleRate = 22050;
  const int amplitude = map(audioVolumePercent, 0, 100, 0, 9000);
  if (amplitude <= 0) {
    lastStatus = "Volume is 0%.";
    return;
  }
  writeAmpSilence(120);
  int freqs[3] = {523, kind == 1 ? 392 : 659, kind == 2 ? 330 : 784};
  Serial.printf("[audio] chime kind=%d volume=%d%% amplitude=%d\n", kind, audioVolumePercent, amplitude);
  for (int f : freqs) {
    const int framesTotal = sampleRate / 3;
    int16_t frames[128 * 2];
    for (int base = 0; base < framesTotal; base += 128) {
      int framesThis = min(128, framesTotal - base);
      for (int i = 0; i < framesThis; i++) {
        int frame = base + i;
        float phase = 2.0f * PI * f * frame / sampleRate;
        float envelope = 1.0f - ((float)frame / framesTotal);
        int16_t sample = (int16_t)(sin(phase) * amplitude * envelope);
        frames[i * 2] = sample;
        frames[i * 2 + 1] = sample;
      }
      size_t written = 0;
      esp_err_t err = i2s_write(I2S_NUM_0, frames, framesThis * 2 * sizeof(int16_t), &written, pdMS_TO_TICKS(100));
      if (err != ESP_OK || written == 0) {
        Serial.printf("[audio] i2s_write err=%d written=%u\n", (int)err, (unsigned)written);
      }
      yield();
    }
  }
  writeAmpSilence(300);
  lastStatus = "Tone played at " + String(audioVolumePercent) + "%.";
  appendSdLog("tone", "volume " + String(audioVolumePercent));
}

static uint16_t readLe16(const uint8_t* p) {
  return (uint16_t)p[0] | ((uint16_t)p[1] << 8);
}

static uint32_t readLe32(const uint8_t* p) {
  return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

bool fetchHubBinary(const String& path, uint8_t** outData, size_t* outLen) {
  *outData = nullptr;
  *outLen = 0;
  if (!WiFi.isConnected()) {
    lastStatus = "No Wi-Fi for speech.";
    return false;
  }

  String url = path.startsWith("http") ? path : hubUrl + path;
  HTTPClient http;
  http.setTimeout(max(8000, WISP_HTTP_TIMEOUT_MS));
  http.setReuse(false);
  bool started = false;
  WiFiClient plain;
  WiFiClientSecure secure;
  if (url.startsWith("https://")) {
    secure.setInsecure();
    started = http.begin(secure, url);
  } else {
    started = http.begin(plain, url);
  }
  if (!started) {
    lastStatus = "Speech URL failed.";
    return false;
  }

  http.addHeader("Accept", "audio/wav");
  http.addHeader("User-Agent", APP_VERSION);
  http.addHeader("X-NodeSparkHub-Device-ID", deviceId);
  http.addHeader("X-NodeSparkHub-Device-Name", deviceName);
  if (token.length()) {
    http.addHeader("Authorization", "Bearer " + token);
    http.addHeader("X-NodeSparkHub-Token", token);
  }

  int code = http.GET();
  int size = http.getSize();
  if (code < 200 || code >= 300 || size <= 0 || size > WISP_MAX_SPEECH_WAV_BYTES) {
    lastStatus = code > 0 ? "Speech HTTP " + String(code) : "Speech offline.";
    http.end();
    return false;
  }

  uint8_t* data = (uint8_t*)ps_malloc(size);
  if (!data) data = (uint8_t*)heap_caps_malloc(size, MALLOC_CAP_8BIT);
  if (!data) {
    lastStatus = "Speech RAM low.";
    http.end();
    return false;
  }

  WiFiClient* stream = http.getStreamPtr();
  int readTotal = 0;
  uint32_t start = millis();
  while (readTotal < size && millis() - start < 15000) {
    int available = stream->available();
    if (available > 0) {
      int chunk = min(available, size - readTotal);
      int got = stream->readBytes(data + readTotal, chunk);
      if (got > 0) {
        readTotal += got;
        start = millis();
      }
    } else {
      delay(2);
      yield();
    }
  }
  http.end();

  if (readTotal != size) {
    free(data);
    lastStatus = "Speech download cut short.";
    return false;
  }
  *outData = data;
  *outLen = (size_t)size;
  return true;
}

bool playSpeechClip(const String& speechPath) {
#if !WISP_ENABLE_AUDIO
  (void)speechPath;
  lastStatus = "Speech needs audio enabled.";
  return false;
#else
  if (!speechPath.length()) return false;
  if (!ampReady) {
    lastStatus = "Amp not ready for speech.";
    return false;
  }
  if (ampMuted || audioVolumePercent <= 0) {
    lastStatus = ampMuted ? "Amp muted." : "Volume is 0%.";
    return false;
  }

  showCard("Wisp Voice", "Downloading Hub speech...", C_PINK);
  uint8_t* wav = nullptr;
  size_t wavLen = 0;
  if (!fetchHubBinary(speechPath, &wav, &wavLen)) return false;

  bool ok = false;
  do {
    if (wavLen < 44 || memcmp(wav, "RIFF", 4) != 0 || memcmp(wav + 8, "WAVE", 4) != 0) break;

    uint16_t audioFormat = 0;
    uint16_t channels = 0;
    uint32_t sampleRate = 0;
    uint16_t bitsPerSample = 0;
    size_t dataOffset = 0;
    size_t dataSize = 0;

    size_t offset = 12;
    while (offset + 8 <= wavLen) {
      const uint8_t* chunk = wav + offset;
      uint32_t chunkSize = readLe32(chunk + 4);
      size_t chunkData = offset + 8;
      if (chunkData + chunkSize > wavLen) break;
      if (memcmp(chunk, "fmt ", 4) == 0 && chunkSize >= 16) {
        audioFormat = readLe16(wav + chunkData);
        channels = readLe16(wav + chunkData + 2);
        sampleRate = readLe32(wav + chunkData + 4);
        bitsPerSample = readLe16(wav + chunkData + 14);
      } else if (memcmp(chunk, "data", 4) == 0) {
        dataOffset = chunkData;
        dataSize = chunkSize;
        break;
      }
      offset = chunkData + chunkSize + (chunkSize & 1);
    }

    if (audioFormat != 1 || bitsPerSample != 16 || channels < 1 || channels > 2 || sampleRate < 8000 || !dataOffset || !dataSize) {
      lastStatus = "Unsupported speech WAV.";
      break;
    }

    i2s_set_sample_rates(I2S_NUM_0, sampleRate);
    writeAmpSilence(80);
    int16_t outFrames[192 * 2];
    size_t pos = dataOffset;
    size_t end = min(wavLen, dataOffset + dataSize);
    int volume = constrain(audioVolumePercent, 0, 100);
    while (pos + channels * 2 <= end) {
      int frames = 0;
      while (frames < 192 && pos + channels * 2 <= end) {
        int16_t left = (int16_t)readLe16(wav + pos);
        int16_t right = left;
        pos += 2;
        if (channels == 2) {
          right = (int16_t)readLe16(wav + pos);
          pos += 2;
        }
        left = (int16_t)(((int32_t)left * volume) / 100);
        right = (int16_t)(((int32_t)right * volume) / 100);
        outFrames[frames * 2] = left;
        outFrames[frames * 2 + 1] = right;
        frames++;
      }
      size_t written = 0;
      i2s_write(I2S_NUM_0, outFrames, frames * 2 * sizeof(int16_t), &written, pdMS_TO_TICKS(250));
      yield();
    }
    writeAmpSilence(220);
    i2s_set_sample_rates(I2S_NUM_0, 22050);
    lastStatus = "AI voice played.";
    appendSdLog("speech_played", speechPath.substring(0, 64));
    ok = true;
  } while (false);

  free(wav);
  if (!ok) {
    i2s_set_sample_rates(I2S_NUM_0, 22050);
  }
  return ok;
#endif
}

bool configureAmpOutput() {
#if !WISP_ENABLE_AUDIO
  ampReady = false;
  ampDriverInstalled = false;
  return false;
#else
  if (ampDriverInstalled) {
    i2s_driver_uninstall(I2S_NUM_0);
    ampDriverInstalled = false;
    ampReady = false;
    delay(20);
  }

  i2s_config_t ampConfig = {};
  ampConfig.mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX);
  ampConfig.sample_rate = 22050;
  ampConfig.bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT;
  ampConfig.channel_format = I2S_CHANNEL_FMT_RIGHT_LEFT;
  ampConfig.communication_format = I2S_COMM_FORMAT_STAND_I2S;
  ampConfig.intr_alloc_flags = ESP_INTR_FLAG_LEVEL1;
  ampConfig.dma_buf_count = 4;
  ampConfig.dma_buf_len = 256;
  ampConfig.use_apll = false;

  const AmpPinMap& map = AMP_PIN_MAPS[currentAmpPinIndex()];
  i2s_pin_config_t ampPins = {map.bclk, map.lrclk, map.din, I2S_PIN_NO_CHANGE};
  esp_err_t ampInstall = i2s_driver_install(I2S_NUM_0, &ampConfig, 0, nullptr);
  esp_err_t ampPin = ampInstall == ESP_OK ? i2s_set_pin(I2S_NUM_0, &ampPins) : ampInstall;
  ampReady = ampInstall == ESP_OK && ampPin == ESP_OK;
  ampDriverInstalled = ampReady;
  if (ampReady) writeAmpSilence(180);
  Serial.printf("[audio] amp mode=%d %s install=%d pin=%d pins bclk=%d lrclk=%d din=%d\n",
                currentAmpPinIndex(),
                map.label,
                (int)ampInstall,
                (int)ampPin,
                map.bclk,
                map.lrclk,
                map.din);
  return ampReady;
#endif
}

void cycleAmpPins() {
  ampPinMode = (currentAmpPinIndex() + 1) % AMP_PIN_MAP_COUNT;
  saveAmpPinMode();
  configureAmpOutput();
  lastStatus = "Amp pins " + ampPinModeLabel();
  appendSdLog("amp_pin_mode", ampPinModeLabel());
}

String commandBody(const HubCommand& command, const String& fallback) {
  if (command.body.length()) return command.body;
  if (command.text.length()) return command.text;
  return fallback;
}

String commandTitle(const HubCommand& command, const String& fallback) {
  return command.title.length() ? command.title : fallback;
}

String commandDetail(const HubCommand& command, const String& fallback = "") {
  if (command.subtitle.length()) return command.subtitle;
  if (command.detail.length()) return command.detail;
  return fallback;
}

uint16_t commandAccent(const HubCommand& command, uint16_t fallback) {
  if (command.rgbR >= 0 && command.rgbG >= 0 && command.rgbB >= 0) {
    return rgb565(command.rgbR, command.rgbG, command.rgbB);
  }
  if (command.style == "success") return C_GREEN;
  if (command.style == "warning" || command.style == "approval") return C_AMBER;
  if (command.style == "error") return C_RED;
  if (command.style == "ai" || command.style == "voice") return C_PINK;
  return fallback;
}

void loadCommand(JsonObject source, HubCommand& command) {
  command.id = bounded(source["id"].as<String>(), 80);
  command.kind = bounded(source["type"].as<String>(), 32);
  command.kind.toLowerCase();
  command.title = bounded(source["title"].as<String>(), 60);
  command.body = bounded(source["body"].as<String>(), 260);
  command.text = bounded(source["text"].as<String>(), 260);
  command.subtitle = bounded(source["subtitle"].as<String>(), 120);
  command.detail = bounded(source["detail"].as<String>(), 160);
  command.style = bounded(source["style"].as<String>(), 32);
  command.style.toLowerCase();
  command.icon = bounded(source["icon"].as<String>(), 32);
  command.qrData = bounded(source["qrData"].as<String>(), 260);
  command.workflowName = bounded(source["workflowName"].as<String>(), 80);
  command.metricLabel = bounded(source["metricLabel"].as<String>(), 48);
  command.metricValue = bounded(source["metricValue"].as<String>(), 48);
  command.itemCount = 0;
  command.rgbR = -1;
  command.rgbG = -1;
  command.rgbB = -1;
  JsonArray rgb = source["rgb"].as<JsonArray>();
  if (!rgb.isNull() && rgb.size() >= 3) {
    command.rgbR = rgb[0].as<int>();
    command.rgbG = rgb[1].as<int>();
    command.rgbB = rgb[2].as<int>();
  }
  JsonArray items = source["items"].as<JsonArray>();
  if (items.isNull() && source["payload"].is<JsonObject>()) {
    items = source["payload"]["items"].as<JsonArray>();
  }
  if (!items.isNull()) {
    for (JsonVariant item : items) {
      if (command.itemCount >= 6) break;
      String label;
      if (item.is<JsonObject>()) {
        label = item["label"].as<String>();
        if (!label.length()) label = item["title"].as<String>();
        if (!label.length()) label = item["name"].as<String>();
      } else {
        label = item.as<String>();
      }
      label.trim();
      if (label.length()) command.items[command.itemCount++] = bounded(label, 48);
    }
  }
}

void executeCommand(const HubCommand& command) {
  String id = command.id;
  String kind = command.kind;
  if (!kind.length()) kind = "display";

  if (kind == "display" || kind == "message" || kind == "show") {
    String title = commandTitle(command, "NodeSparkHub");
    String body = commandBody(command);
    showCard(title, body, commandAccent(command, C_BLUE));
    lastCommand = title + ": " + body;
    playChime(0);
    ackCommand(id, "completed", "displayed");
  } else if (kind == "assistant" || kind == "ask" || kind == "askai") {
    String text = commandBody(command, "Help me from NodeSpark Wisp.");
    askAssistant(text);
    lastCommand = "Assistant: " + text;
    ackCommand(id, "completed", "assistant requested");
  } else if (kind == "runworkflow" || kind == "run" || kind == "workflow") {
    String previousWorkflow = defaultWorkflow;
    if (command.workflowName.length()) defaultWorkflow = command.workflowName;
    String body = commandBody(command, "ESP32-S3 Wisp command requested a workflow.");
    runWorkflow(body);
    if (command.workflowName.length()) defaultWorkflow = previousWorkflow;
    lastCommand = "Workflow: " + (command.workflowName.length() ? command.workflowName : previousWorkflow);
    ackCommand(id, "completed", "workflow requested");
  } else if (kind == "selectworkflow" || kind == "select") {
    if (command.workflowName.length()) {
      defaultWorkflow = command.workflowName;
      clampSetting(defaultWorkflow, 80);
      prefs.putString("workflow", defaultWorkflow);
    }
    showCard("Workflow Selected", defaultWorkflow.length() ? defaultWorkflow : "Wisp Assistant", C_GREEN);
    lastCommand = "Selected workflow: " + defaultWorkflow;
    ackCommand(id, "completed", defaultWorkflow);
  } else if (kind == "card" || kind == "alert" || kind == "success" || kind == "warning" || kind == "error" || kind == "ai" || kind == "voice" || kind == "timer" || kind == "weather" || kind == "statuscard" || kind == "promo") {
    String title = commandTitle(command, "NodeSparkHub Card");
    String body = commandBody(command);
    String detail = commandDetail(command);
    if (detail.length()) body = detail + "\n" + body;
    showCard(title, body, commandAccent(command, C_PINK));
    lastCommand = "Card: " + title;
    playChime(0);
    ackCommand(id, "completed", "card shown");
  } else if (kind == "demo" || kind == "showcase" || kind == "salesdemo") {
    runDemoSequence(command);
    lastCommand = "Demo played";
    ackCommand(id, "completed", "demo played");
  } else if (kind == "graphic" || kind == "graphics" || kind == "icons" || kind == "icongrid") {
    drawIconGrid(commandTitle(command, "NodeSpark Powers"), command.items, command.itemCount, commandAccent(command, C_BLUE));
    lastCommand = "Graphics shown";
    playChime(0);
    ackCommand(id, "completed", "graphics shown");
  } else if (kind == "dashboard" || kind == "metrics" || kind == "monitor") {
    drawDashboard(commandTitle(command, "Workflow Monitor"), command.metricLabel.length() ? command.metricLabel : "Hub", command.metricValue.length() ? command.metricValue : "Live", command.items, command.itemCount, commandAccent(command, C_GREEN));
    lastCommand = "Dashboard shown";
    playChime(0);
    ackCommand(id, "completed", "dashboard shown");
  } else if (kind == "health" || kind == "status" || kind == "devicehealth") {
    drawHealthDashboard();
    lastCommand = "Health shown";
    ackCommand(id, "completed", "health shown");
  } else if (kind == "notify" || kind == "notification" || kind == "inbox") {
    pushNotification(commandTitle(command, "NodeSparkHub"), commandBody(command, "New Hub notification."));
    drawNotificationStack(commandAccent(command, C_BLUE));
    lastCommand = "Notification saved";
    playChime(0);
    ackCommand(id, "completed", String(notificationCount) + " notifications saved");
  } else if (kind == "approval" || kind == "approve" || kind == "decision") {
    pendingApprovalId = id;
    pendingApprovalTitle = commandTitle(command, "Approval Needed");
    pendingApprovalBody = commandBody(command, "Review this request.");
    currentScreen = SCREEN_COMMANDS;
    drawCommands();
    playChime(2);
  } else if (kind == "speak" || kind == "speaker" || kind == "tts") {
    String text = commandBody(command);
    showCard("Speaking", text, C_PINK);
    playChime(0);
    ackCommand(id, "completed", "chime played; TTS pending");
  } else if (kind == "led" || kind == "rgb" || kind == "setled") {
    uint16_t accent = commandAccent(command, C_BLUE);
    showCard("LED Color", "ESP32 Wisp used the requested color as its screen accent.", accent);
    playChime(0);
    ackCommand(id, "completed", "accent color applied");
  } else if (kind == "splash" || kind == "logo" || kind == "startup") {
    drawSplash(commandBody(command, "NodeSparkHub physical node"));
    playChime(1);
    ackCommand(id, "completed", "startup logo");
  } else if (kind == "qr" || kind == "pairingqr") {
    String data = command.qrData.length() ? command.qrData : commandBody(command, hubBase.length() ? hubBase : "nodesparkhub-device://pair");
    drawQrText(commandTitle(command, "NodeSpark QR"), data, commandDetail(command, "Scan or copy this link"), commandAccent(command, C_BLUE));
    ackCommand(id, "completed", "qr shown");
  } else if (kind == "ping") {
    showCard("Ping", "NodeSparkHub is talking to this ESP32-S3 Wisp.", C_GREEN);
    playChime(0);
    ackCommand(id, "completed", "pong");
  } else {
    lastCommand = "Unknown command: " + kind;
    ackCommand(id, "ignored", lastCommand);
    redraw();
  }
}

#if WISP_ENABLE_BLE
String bleStateJson() {
  String body = "{";
  body += "\"type\":\"state\",";
  body += "\"deviceId\":\"" + jsonEscape(deviceId) + "\",";
  body += "\"deviceName\":\"" + jsonEscape(deviceName) + "\",";
  body += "\"workflowName\":\"" + jsonEscape(defaultWorkflow) + "\",";
  body += "\"pairedToHub\":" + String(token.length() ? "true" : "false") + ",";
  body += "\"wifi\":" + String(WiFi.isConnected() ? "true" : "false") + ",";
  body += "\"bridge\":\"wisp-esp32-ble\"}";
  return body;
}

void bleNotifyState() {
  if (!bleReady || !bleStateCharacteristic) return;
  String state = bleStateJson();
  bleStateCharacteristic->setValue((uint8_t*)state.c_str(), state.length());
  bleStateCharacteristic->notify();
}

void bleNotifyEvent(const String& type, const String& detail) {
  if (!bleReady || !bleEventCharacteristic) return;
  String event = "{\"type\":\"" + jsonEscape(type) + "\",\"detail\":\"" + jsonEscape(detail) + "\"}";
  bleEventCharacteristic->setValue((uint8_t*)event.c_str(), event.length());
  bleEventCharacteristic->notify();
}

class WispBleCommandCallbacks : public BLECharacteristicCallbacks {
  void onWrite(BLECharacteristic* characteristic) override {
    String raw = characteristic->getValue().c_str();
    raw.trim();
    if (!raw.length()) return;
    size_t n = min((size_t)raw.length(), sizeof(pendingBleRaw) - 1);
    memcpy(pendingBleRaw, raw.c_str(), n);
    pendingBleRaw[n] = '\0';
    pendingBleCommand = true;
  }
};

void processBleCommand() {
  if (!pendingBleCommand || actionBusy) return;
  char raw[sizeof(pendingBleRaw)];
  noInterrupts();
  strncpy(raw, pendingBleRaw, sizeof(raw));
  raw[sizeof(raw) - 1] = '\0';
  pendingBleCommand = false;
  interrupts();

  StaticJsonDocument<1024> doc;
  DeserializationError err = deserializeJson(doc, raw);
  if (err || !doc.is<JsonObject>()) {
    bleNotifyEvent("error", "Bad BLE JSON");
    return;
  }
  actionBusy = true;
  HubCommand command;
  loadCommand(doc.as<JsonObject>(), command);
  executeCommand(command);
  actionBusy = false;
  bleNotifyEvent("command", command.kind.length() ? command.kind : "display");
  bleNotifyState();
}

void setupBleBridge() {
  BLEDevice::init("NodeSpark Wisp");
  BLEServer* server = BLEDevice::createServer();
  BLEService* service = server->createService(BLE_SERVICE_UUID);

  BLECharacteristic* commandCharacteristic = service->createCharacteristic(
    BLE_COMMAND_UUID,
    BLECharacteristic::PROPERTY_WRITE | BLECharacteristic::PROPERTY_WRITE_NR
  );
  commandCharacteristic->setCallbacks(new WispBleCommandCallbacks());

  bleEventCharacteristic = service->createCharacteristic(
    BLE_EVENT_UUID,
    BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_NOTIFY
  );
  bleEventCharacteristic->addDescriptor(new BLE2902());

  bleStateCharacteristic = service->createCharacteristic(
    BLE_STATE_UUID,
    BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_NOTIFY
  );
  bleStateCharacteristic->addDescriptor(new BLE2902());

  String state = bleStateJson();
  bleEventCharacteristic->setValue((uint8_t*)state.c_str(), state.length());
  bleStateCharacteristic->setValue((uint8_t*)state.c_str(), state.length());

  service->start();
  BLEAdvertising* advertising = BLEDevice::getAdvertising();
  advertising->addServiceUUID(BLE_SERVICE_UUID);
  advertising->setScanResponse(true);
  advertising->setMinPreferred(0x06);
  advertising->setMinPreferred(0x12);
  BLEDevice::startAdvertising();
  bleReady = true;
  Serial.println("[ble] Wisp Mobile Bridge advertising");
}
#else
void bleNotifyState() {}
void processBleCommand() {}
void setupBleBridge() {
  bleReady = false;
  Serial.println("[ble] Wisp Mobile Bridge disabled in this stability build");
}
#endif

void pollCommands() {
  if (!token.length()) return;
  String payload = request("GET", "/devices/" + deviceId + "/commands/poll?limit=4");
  if (!payload.length()) return;
  HubCommand queue[4];
  int queued = 0;
  {
    DynamicJsonDocument doc(4096);
    if (deserializeJson(doc, payload)) {
      lastStatus = "Bad command JSON.";
      return;
    }
    JsonArray commands = doc["commands"].as<JsonArray>();
    for (JsonObject command : commands) {
      if (queued >= 4) break;
      loadCommand(command, queue[queued]);
      queued++;
    }
  }
  for (int i = 0; i < queued; i++) {
    executeCommand(queue[i]);
    yield();
  }
}

String resolveHubWorkflow(const String& preferred) {
  String payload = request("GET", "/workflows");
  if (!payload.length()) return preferred;

  StaticJsonDocument<2048> doc;
  if (deserializeJson(doc, payload)) return preferred;
  JsonArray workflows = doc["workflows"].as<JsonArray>();
  if (workflows.isNull() || workflows.size() == 0) {
    lastStatus = "No Hub workflows found.";
    return "";
  }

  String preferredNorm = normalizedWorkflowName(preferred);
  String first;
  String assistantMatch;
  for (JsonVariant item : workflows) {
    String name = bounded(item.as<String>(), 80);
    if (!name.length()) continue;
    if (!first.length()) first = name;
    String norm = normalizedWorkflowName(name);
    if (norm == preferredNorm) return name;
    if (!assistantMatch.length() && (norm.indexOf("wisp") >= 0 || norm.indexOf("assistant") >= 0 || norm.indexOf("ai") >= 0)) {
      assistantMatch = name;
    }
  }

  if (assistantMatch.length()) {
    lastStatus = "Using workflow " + assistantMatch;
    return assistantMatch;
  }
  lastStatus = "Using workflow " + first;
  return first;
}

void runWorkflow(const String& text) {
  if (!WiFi.isConnected()) {
    lastStatus = "Connect Wi-Fi from Set > Conn first.";
    showCard("Wi-Fi Needed", lastStatus, C_AMBER);
    return;
  }
  if (!token.length()) {
    lastStatus = "Pair device before running workflows.";
    showCard("Pair Required", "Open Pair, enter the NodeSparkHub device code, then try Ask AI again.", C_AMBER);
    return;
  }
  String workflowName = resolveHubWorkflow(defaultWorkflow);
  if (!workflowName.length()) {
    showCard("No Workflows", "NodeSparkHub is connected, but no workflows are available yet.", C_AMBER);
    return;
  }

  String path = "/workflows/" + urlEncodePath(workflowName) + "/run";
#if WISP_ASYNC_WORKFLOWS
  path += "?async=1";
#endif
  String body = "{";
  body += "\"source\":\"wisp-esp32\",";
  body += "\"deviceId\":\"" + jsonEscape(deviceId) + "\",";
  body += "\"deviceName\":\"" + jsonEscape(deviceName) + "\",";
  body += "\"workflow\":\"" + jsonEscape(workflowName) + "\",";
  body += "\"wispEvent\":\"askAI\",";
  body += "\"text\":\"" + jsonEscape(text) + "\",";
  body += "\"input\":\"" + jsonEscape(text) + "\"}";
  String payload = request("POST", path, body);
  if (payload.length()) {
    StaticJsonDocument<2048> doc;
    String output;
    if (!deserializeJson(doc, payload)) {
      output = doc["output"].as<String>();
      if (!output.length()) output = doc["result"].as<String>();
      if (!output.length()) output = doc["message"].as<String>();
      if (!output.length()) output = doc["status"].as<String>();
      if (!output.length()) output = doc["runId"].as<String>();
    }
    lastStatus = "Workflow sent to Hub.";
    appendSdLog("workflow_sent", workflowName);
#if WISP_ASYNC_WORKFLOWS
    if (output.length()) showCard(workflowName, "Sent to NodeSparkHub. Run " + output.substring(0, 48), C_GREEN);
    else showCard(workflowName, "Sent to NodeSparkHub. Watch the Hub run history for the AI response.", C_GREEN);
#else
    if (output.length()) showCard(workflowName, output.substring(0, 180), C_PINK);
    else showCard(workflowName, "Workflow sent to NodeSparkHub. No text response was returned yet.", C_GREEN);
#endif
  } else {
    String failure = lastStatus.length() ? lastStatus : "Workflow failed.";
    lastStatus = failure;
    showCard("Workflow Failed", failure, C_RED);
  }
}

void askAssistant(const String& text) {
  showCard("Wisp Assistant", "Sending to NodeSparkHub AI...", C_PINK);
  if (askHubAssistant(text)) return;
  showCard("Wisp Assistant", "Direct AI did not answer. Trying the Wisp workflow...", C_AMBER);
  runWorkflow(text);
}

bool askHubAssistant(const String& text) {
  if (!WiFi.isConnected()) {
    lastStatus = "Connect Wi-Fi from Set > Conn first.";
    showCard("Wi-Fi Needed", lastStatus, C_AMBER);
    return true;
  }
  if (!token.length()) {
    lastStatus = "Pair device before using Wisp Assistant.";
    showCard("Pair Required", "Open Pair, enter the NodeSparkHub device code, then try Ask AI again.", C_AMBER);
    return true;
  }

  String body = "{";
  body += "\"deviceId\":\"" + jsonEscape(deviceId) + "\",";
  body += "\"deviceName\":\"" + jsonEscape(deviceName) + "\",";
  body += "\"text\":\"" + jsonEscape(text) + "\",";
  body += "\"source\":\"wisp-esp32-touch\",";
  body += "\"platform\":\"ESP32-S3 / NodeSpark Wisp Touch\",";
  body += "\"sessionId\":\"esp32:" + jsonEscape(deviceId) + "\",";
  body += "\"voice\":true,";
  body += "\"capabilities\":[\"display\",\"touch\",\"speaker\",\"microphone\",\"assistant\",\"workflow\",\"sd\"]}";

  String payload = request("POST", "/wisp/assistant", body);
  if (!payload.length()) return false;

  DynamicJsonDocument doc(4096);
  if (deserializeJson(doc, payload)) {
    lastStatus = "Bad AI reply JSON.";
    return false;
  }

  String reply = doc["displayText"].as<String>();
  if (!reply.length()) reply = doc["reply"].as<String>();
  if (!reply.length()) reply = doc["message"].as<String>();
  if (!reply.length()) reply = doc["error"].as<String>();
  if (!reply.length()) reply = "NodeSparkHub AI answered with an empty response.";
  String speechText = doc["speechText"].as<String>();
  if (!speechText.length()) speechText = doc["reply"].as<String>();
  String speechPath = doc["speechPath"].as<String>();
  if (!speechPath.length()) speechPath = doc["speechURL"].as<String>();
  bool shouldSpeak = !doc["shouldSpeak"].is<bool>() || doc["shouldSpeak"].as<bool>();

  lastStatus = doc["ok"].as<bool>() ? "AI assistant answered." : "AI assistant error.";
  showCard(doc["ok"].as<bool>() ? "Wisp Assistant" : "AI Setup Needed", reply.substring(0, 220), doc["ok"].as<bool>() ? C_PINK : C_AMBER);
  if (doc["ok"].as<bool>()) {
    if (!shouldSpeak || !speechPath.length() || !playSpeechClip(speechPath)) {
      playChime(1);
    }
    if (speechText.length()) appendSdLog("assistant_speech", speechText.substring(0, 80));
    appendSdLog("assistant_reply", reply.substring(0, 80));
  }
  return true;
}

int sampleMicLevel() {
  if (!micReady) {
    lastMicBytes = 0;
    lastMicLevel = 0;
    return 0;
  }
  int32_t samples[384];
  size_t bytesRead = 0;
  i2s_read(I2S_NUM_1, samples, sizeof(samples), &bytesRead, pdMS_TO_TICKS(120));
  lastMicBytes = (int)bytesRead;
  int count = bytesRead / sizeof(int32_t);
  if (count <= 0) {
    lastMicLevel = 0;
    Serial.println("[audio] mic read returned no samples");
    return 0;
  }
  int32_t minSample = INT32_MAX;
  int32_t maxSample = INT32_MIN;
  int64_t sum = 0;
  for (int i = 0; i < count; i++) {
    int32_t sample = samples[i] >> 8;  // INMP441 sends 24-bit samples left-aligned in 32-bit words.
    minSample = min(minSample, sample);
    maxSample = max(maxSample, sample);
    sum += sample;
  }
  int32_t mean = (int32_t)(sum / count);
  uint64_t activity = 0;
  for (int i = 0; i < count; i++) {
    int32_t sample = samples[i] >> 8;
    activity += abs(sample - mean);
  }
  int32_t peakToPeak = maxSample - minSample;
  int rmsLike = (int)(activity / count / 2048);
  int peakLike = peakToPeak / 8192;
  lastMicLevel = constrain(max(rmsLike, peakLike), 0, 1023);
  Serial.printf("[audio] mic level=%d bytes=%d count=%d min=%ld max=%ld mean=%ld\n",
                lastMicLevel,
                lastMicBytes,
                count,
                (long)minSample,
                (long)maxSample,
                (long)mean);
  return lastMicLevel;
}

void handleTouch(int x, int y) {
  if (currentScreen == SCREEN_SETUP && setupView == SETUP_INPUT) {
    handleSetupTouch(x, y);
    return;
  }
  if (y >= 204) {
    if (x < 54) currentScreen = SCREEN_STATUS;
    else if (x < 106) currentScreen = SCREEN_PAIR;
    else if (x < 158) currentScreen = SCREEN_COMMANDS;
    else if (x < 210) currentScreen = SCREEN_DEMO;
    else if (x < 262) currentScreen = SCREEN_MIC;
    else {
      currentScreen = SCREEN_SETUP;
      setupView = SETUP_MAIN;
    }
    redraw();
    return;
  }

  if (currentScreen == SCREEN_SETUP) {
    handleSetupTouch(x, y);
  } else if (currentScreen == SCREEN_PAIR) {
    int digit = -1;
    for (int row = 0; row < 3; row++) {
      for (int col = 0; col < 3; col++) {
        int n = row * 3 + col + 1;
        if (inBox(x, y, {170 + col * 46, 52 + row * 38, 38, 30, "", C_BLUE})) digit = n;
      }
    }
    if (inBox(x, y, {216, 166, 38, 30, "", C_BLUE})) digit = 0;
    if (digit >= 0 && pairCode.length() < 8) pairCode += String(digit);
    if (inBox(x, y, {88, 116, 66, 34, "", C_AMBER})) pairCode = "";
    if (inBox(x, y, {14, 116, 68, 34, "", C_GREEN})) pairHub();
    drawPair();
  } else if (currentScreen == SCREEN_COMMANDS && pendingApprovalId.length()) {
    if (inBox(x, y, {24, 150, 120, 36, "", C_GREEN})) {
      ackCommand(pendingApprovalId, "approved", "approved on ESP32 touchscreen");
      pendingApprovalId = "";
      lastCommand = "Approval accepted";
      playChime(0);
      drawCommands();
    } else if (inBox(x, y, {176, 150, 120, 36, "", C_RED})) {
      ackCommand(pendingApprovalId, "rejected", "rejected on ESP32 touchscreen");
      pendingApprovalId = "";
      lastCommand = "Approval rejected";
      playChime(1);
      drawCommands();
    }
  } else if (currentScreen == SCREEN_DEMO) {
    if (inBox(x, y, {14, 56, 136, 42, "", C_GREEN})) {
      showCard("Ping", "Local touch ping. Hub polling is active.", C_GREEN);
      playChime(0);
    } else if (inBox(x, y, {170, 56, 136, 42, "", C_BLUE})) {
      runGuardedAction("Asking AI...", [](){ askAssistant("Give a short exciting demo of what NodeSpark Wisp can do."); });
    } else if (inBox(x, y, {14, 112, 136, 42, "", C_AMBER})) {
      runGuardedAction("Workflow...", [](){ runWorkflow("ESP32-S3 Wisp touchscreen requested a workflow."); });
    } else if (inBox(x, y, {170, 112, 136, 42, "", C_PINK})) {
      playChime(2);
    }
  } else if (currentScreen == SCREEN_MIC) {
    if (inBox(x, y, {14, 146, 54, 28, "", C_PANEL})) {
      adjustAudioVolume(-10);
      drawMic();
    } else if (inBox(x, y, {76, 146, 54, 28, "", C_PANEL})) {
      adjustAudioVolume(10);
      drawMic();
    } else if (inBox(x, y, {138, 146, 54, 28, "", C_AMBER})) {
      ampMuted = false;
      cycleAmpPins();
      drawMic();
      playChime(0);
      drawMic();
    } else if (inBox(x, y, {200, 146, 48, 28, "", C_PINK})) {
      ampMuted = false;
      playChime(0);
      drawMic();
    } else if (inBox(x, y, {256, 146, 50, 28, "", C_BLUE})) {
      cycleMicMode();
      int level = sampleMicLevel();
      drawMic();
      drawMicMeter(level);
    } else if (inBox(x, y, {14, 178, 84, 24, "", C_PANEL})) {
      setAmpMuted(!ampMuted);
      drawMic();
    } else if (inBox(x, y, {108, 178, 198, 24, "", C_AMBER})) {
      runWorkflow("ESP32-S3 Wisp voice button pressed. Audio upload support will be added next.");
    }
  }
}

bool configureMicInput() {
#if !WISP_ENABLE_AUDIO
  micReady = false;
  micDriverInstalled = false;
  return false;
#else
  if (micDriverInstalled) {
    i2s_driver_uninstall(I2S_NUM_1);
    micDriverInstalled = false;
    micReady = false;
    delay(20);
  }

  i2s_config_t micConfig = {};
  micConfig.mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_RX);
  micConfig.sample_rate = 16000;
  micConfig.bits_per_sample = I2S_BITS_PER_SAMPLE_32BIT;
  micConfig.channel_format = MIC_CHANNEL_FORMATS[currentMicModeIndex()];
  micConfig.communication_format = I2S_COMM_FORMAT_STAND_I2S;
  micConfig.intr_alloc_flags = ESP_INTR_FLAG_LEVEL1;
  micConfig.dma_buf_count = 4;
  micConfig.dma_buf_len = 256;
  micConfig.use_apll = false;
  i2s_pin_config_t micPins = {PIN_MIC_SCK, PIN_MIC_WS, I2S_PIN_NO_CHANGE, PIN_MIC_SD};
  esp_err_t micInstall = i2s_driver_install(I2S_NUM_1, &micConfig, 0, nullptr);
  esp_err_t micPin = micInstall == ESP_OK ? i2s_set_pin(I2S_NUM_1, &micPins) : micInstall;
  micReady = micInstall == ESP_OK && micPin == ESP_OK;
  micDriverInstalled = micReady;
  Serial.printf("[audio] mic mode=%s install=%d pin=%d pins sck=%d ws=%d sd=%d\n",
                micModeLabel().c_str(),
                (int)micInstall,
                (int)micPin,
                PIN_MIC_SCK,
                PIN_MIC_WS,
                PIN_MIC_SD);
  return micReady;
#endif
}

void setupAudio() {
#if !WISP_ENABLE_AUDIO
  ampReady = false;
  micReady = false;
  Serial.println("[audio] disabled in firmware; set WISP_ENABLE_AUDIO=1 after wiring amp/mic");
  return;
#endif
  configureAmpOutput();
  configureMicInput();
}

void setupWifi() {
#if WISP_CONNECT_ON_BOOT
  startWifiConnect(true);
#else
  WiFi.mode(WIFI_OFF);
  lastStatus = "Open Set > Conn for Wi-Fi.";
  Serial.println("[wifi] auto-connect disabled for stable hardware bring-up");
#endif
}

void setup() {
  Serial.begin(115200);
  delay(200);
  Serial.println();
  Serial.println("[boot] NodeSpark Wisp ESP32-S3 starting");
  Serial.printf("[boot] reset reason: %s\n", resetReasonName(esp_reset_reason()).c_str());
  prefs.begin("wisp", false);
  deviceId = prefs.getString("deviceId", "");
  String stableDeviceId = deviceUuidFromMac();
  if (deviceId != stableDeviceId) {
    deviceId = stableDeviceId;
    prefs.putString("deviceId", deviceId);
    prefs.remove("token");
    prefs.remove("hubId");
  }
  Serial.printf("[boot] deviceId=%s\n", deviceId.c_str());
  token = prefs.getString("token", "");
  audioVolumePercent = constrain(prefs.getInt("audioVol", 90), 0, 100);
  micChannelMode = constrain(prefs.getInt("micMode", 0), 0, MIC_CHANNEL_MODE_COUNT - 1);
#if WISP_FORCE_DEFAULT_AMP_PINS
  ampPinMode = 0;
  prefs.putInt("ampPinMode", ampPinMode);
#else
  ampPinMode = constrain(prefs.getInt("ampPinMode", 0), 0, AMP_PIN_MAP_COUNT - 1);
#endif
  loadNetworkSettings();

  Serial.println("[display] init");
  tft.init();
  tft.setRotation(1);
  drawSplash("Starting physical workflows");
  Serial.println("[display] splash complete");
  touch.begin(SPI);
  touch.setRotation(1);
  digitalWrite(PIN_TOUCH_CS, HIGH);
  delay(1100);

  setupAudio();
  Serial.printf("[audio] amp=%s mic=%s\n", ampReady ? "ready" : "off", micReady ? "ready" : "off");
  setupBleBridge();
  setupWifi();
  redraw();
  playChime(0);
  Serial.println("[boot] ready");
}

void loop() {
  uint32_t now = millis();
  serviceWifiConnect();

  if (!actionBusy && now - lastTouchPollMs >= WISP_TOUCH_POLL_MS) {
    lastTouchPollMs = now;
    int x, y;
    if (touched(x, y)) {
      if (!touchDown && now - lastTouchHandledMs > 320) {
        touchDown = true;
        lastTouchHandledMs = now;
        Serial.printf("[touch] x=%d y=%d screen=%d setup=%d\n", x, y, (int)currentScreen, (int)setupView);
        handleTouch(x, y);
      }
    } else {
      touchDown = false;
    }
  }

  processBleCommand();

  if (!actionBusy && currentScreen == SCREEN_MIC && now - lastMicSampleMs >= 260) {
    lastMicSampleMs = now;
    sampleMicLevel();
    drawMicMeter(lastMicLevel);
  }

#if WISP_ENABLE_HUB_HEARTBEAT
  if (!actionBusy && WiFi.isConnected() && token.length() && now - lastCheckinMs > WISP_HUB_HEARTBEAT_MS) {
    lastCheckinMs = millis();
    checkin();
  }
#endif
#if WISP_ENABLE_BACKGROUND_HUB_POLL
  if (!actionBusy && WiFi.isConnected() && token.length() && now - lastPollMs > WISP_COMMAND_POLL_MS) {
    lastPollMs = millis();
    pollCommands();
    bleNotifyState();
  }
#endif
  // Avoid periodic full-screen redraws; they look like black flashes on ILI9341.
  yield();
}
