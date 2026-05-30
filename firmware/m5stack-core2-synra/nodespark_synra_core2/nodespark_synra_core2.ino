#include <Arduino.h>
#include <ArduinoJson.h>
#include <DNSServer.h>
#include <HTTPClient.h>
#include <M5Unified.h>
#include <Preferences.h>
#include <SD.h>
#include <WebServer.h>
#include <WiFi.h>
#include <WiFiClient.h>
#include <WiFiClientSecure.h>

#if __has_include("config.h")
#include "config.h"
#else
#define WISP_WIFI_SSID ""
#define WISP_WIFI_PASSWORD ""
#define WISP_HUB_URL "https://nodespark.msidragon.com"
#define WISP_DEVICE_NAME "NodeSpark Synra Core2"
#define WISP_DEFAULT_WORKFLOW "Synra Voice Remote"
#define WISP_CONNECT_ON_BOOT 1
#define WISP_HTTP_TIMEOUT_MS 9000
#define WISP_HUB_HEARTBEAT_MS 30000
#define WISP_COMMAND_POLL_MS 8000
#define WISP_ENABLE_SD 1
#define WISP_ENABLE_MIC 1
#define WISP_ENABLE_SPEAKER 1
#define WISP_ENABLE_HAPTICS 1
#define WISP_ENABLE_BLE 1
#endif

#ifndef WISP_HTTP_TIMEOUT_MS
#define WISP_HTTP_TIMEOUT_MS 9000
#endif
#ifndef WISP_HUB_HEARTBEAT_MS
#define WISP_HUB_HEARTBEAT_MS 30000
#endif
#ifndef WISP_COMMAND_POLL_MS
#define WISP_COMMAND_POLL_MS 8000
#endif
#ifndef WISP_ENABLE_BLE
#define WISP_ENABLE_BLE 1
#endif

#if WISP_ENABLE_BLE
#include <BLE2902.h>
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#endif

static constexpr uint16_t C_BG = 0x0841;
static constexpr uint16_t C_PANEL = 0x10A4;
static constexpr uint16_t C_PANEL2 = 0x2128;
static constexpr uint16_t C_TEXT = 0xFFFF;
static constexpr uint16_t C_MUTED = 0xBDF7;
static constexpr uint16_t C_CYAN = 0x07FF;
static constexpr uint16_t C_BLUE = 0x349F;
static constexpr uint16_t C_PINK = 0xF81F;
static constexpr uint16_t C_GREEN = 0x47E8;
static constexpr uint16_t C_AMBER = 0xFDA0;
static constexpr uint16_t C_RED = 0xF9E7;
static constexpr int W = 320;
static constexpr int H = 240;
static constexpr int HEADER_H = 36;
static constexpr int NAV_Y = 198;
static constexpr int NAV_H = 42;
static constexpr byte DNS_PORT = 53;
static constexpr uint32_t VOICE_MS = 5500;
static constexpr uint32_t VOICE_RATE = 16000;
static constexpr const char* APP_VERSION = "nodespark-synra-core2/2.0.0-clean";
static constexpr int SYNRA_AVATAR_W = 64;
static constexpr int SYNRA_AVATAR_H = 136;

#if WISP_ENABLE_BLE
static constexpr const char* BLE_SERVICE_UUID = "4E530001-4E53-5749-5350-000000000001";
static constexpr const char* BLE_COMMAND_UUID = "4E530002-4E53-5749-5350-000000000001";
static constexpr const char* BLE_EVENT_UUID = "4E530003-4E53-5749-5350-000000000001";
static constexpr const char* BLE_STATE_UUID = "4E530004-4E53-5749-5350-000000000001";
#endif

enum Screen : uint8_t {
  SCREEN_HOME,
  SCREEN_AI,
  SCREEN_PAIR,
  SCREEN_HARDWARE,
  SCREEN_SETUP,
  SCREEN_COUNT
};

struct Btn {
  int x;
  int y;
  int w;
  int h;
  const char* label;
};

struct HttpResult {
  int status = -1;
  String body;
  String error;
};

Preferences prefs;
WebServer portal(80);
DNSServer dns;

Screen screen = SCREEN_HOME;
bool dirty = true;
bool portalActive = false;
bool hubOnline = false;
bool micReady = false;
bool speakerReady = false;
bool sdReady = false;
bool bleReady = false;
bool recording = false;
int volumeLevel = 110;
int micLevel = 0;
uint32_t lastCheckinMs = 0;
uint32_t lastPollMs = 0;
uint32_t lastMeterMs = 0;
uint32_t lastUiTouchMs = 0;
String wifiSsid = WISP_WIFI_SSID;
String wifiPass = WISP_WIFI_PASSWORD;
String hubBase = WISP_HUB_URL;
String deviceName = WISP_DEVICE_NAME;
String deviceId;
String token;
String pairCode;
int characterIndex = 0;
String lastStatus = "Starting";
String lastCommand = "None";
String lastTranscript;
String lastAssistantReply;
String portalApName;

#if WISP_ENABLE_BLE
BLECharacteristic* bleState = nullptr;
BLECharacteristic* bleEvent = nullptr;
char bleCommandRaw[640] = {0};
volatile bool hasBleCommand = false;
#endif

Btn navBtns[] = {
  {0, NAV_Y, 64, NAV_H, "Home"},
  {64, NAV_Y, 64, NAV_H, "AI"},
  {128, NAV_Y, 64, NAV_H, "Pair"},
  {192, NAV_Y, 64, NAV_H, "Test"},
  {256, NAV_Y, 64, NAV_H, "Set"}
};

String jsonEscape(const String& value);
String urlEncode(const String& value);
void drawScreen();
void showMessage(const String& title, const String& body, uint16_t accent);
void startVoiceAssistant();
void askAssistant(const String& prompt);
bool playSpeechPath(const String& path);
void playChime(uint16_t accent = C_CYAN);
bool emergencyPowerOrStop(bool allowStopOnly = false);
String selectedCharacterName();
void cycleCharacter();

const char* SYNRA_CHARACTERS[] = {
  "Synra",
  "Synra Modern",
  "Synra Battle"
};
static constexpr int SYNRA_CHARACTER_COUNT = sizeof(SYNRA_CHARACTERS) / sizeof(SYNRA_CHARACTERS[0]);

String selectedCharacterName() {
  int idx = constrain(characterIndex, 0, SYNRA_CHARACTER_COUNT - 1);
  return String(SYNRA_CHARACTERS[idx]);
}

void cycleCharacter() {
  characterIndex = (characterIndex + 1) % SYNRA_CHARACTER_COUNT;
  prefs.putInt("character", characterIndex);
  lastStatus = "Character: " + selectedCharacterName();
  dirty = true;
}

String jsonEscape(const String& value) {
  String out;
  out.reserve(value.length() + 8);
  for (size_t i = 0; i < value.length(); i++) {
    char c = value[i];
    if (c == '\\') out += "\\\\";
    else if (c == '"') out += "\\\"";
    else if (c == '\n') out += "\\n";
    else if (c == '\r') out += "\\r";
    else if (c == '\t') out += "\\t";
    else out += c;
  }
  return out;
}

String htmlEscape(const String& value) {
  String out;
  for (size_t i = 0; i < value.length(); i++) {
    char c = value[i];
    if (c == '&') out += F("&amp;");
    else if (c == '<') out += F("&lt;");
    else if (c == '>') out += F("&gt;");
    else if (c == '"') out += F("&quot;");
    else out += c;
  }
  return out;
}

String urlEncode(const String& value) {
  String out;
  const char* hex = "0123456789ABCDEF";
  for (size_t i = 0; i < value.length(); i++) {
    uint8_t c = value[i];
    if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') out += char(c);
    else {
      out += '%';
      out += hex[c >> 4];
      out += hex[c & 0x0F];
    }
  }
  return out;
}

String trimBase(String value) {
  value.trim();
  while (value.endsWith("/")) value.remove(value.length() - 1);
  if (!value.startsWith("http://") && !value.startsWith("https://")) value = "https://" + value;
  return value;
}

String makeDeviceId() {
  uint64_t mac = ESP.getEfuseMac();
  char buf[37];
  snprintf(buf, sizeof(buf), "4e530000-%04x-%04x-%04x-%012llx",
           (uint16_t)((mac >> 32) & 0xFFFF),
           (uint16_t)((mac >> 16) & 0xFFFF),
           (uint16_t)(mac & 0xFFFF),
           (unsigned long long)(mac & 0xFFFFFFFFFFFFULL));
  return String(buf);
}

static void le16(uint8_t* p, uint16_t v) {
  p[0] = v & 0xFF;
  p[1] = (v >> 8) & 0xFF;
}

static void le32(uint8_t* p, uint32_t v) {
  p[0] = v & 0xFF;
  p[1] = (v >> 8) & 0xFF;
  p[2] = (v >> 16) & 0xFF;
  p[3] = (v >> 24) & 0xFF;
}

void writeWavHeader(uint8_t* wav, uint32_t dataBytes) {
  memcpy(wav, "RIFF", 4);
  le32(wav + 4, 36 + dataBytes);
  memcpy(wav + 8, "WAVEfmt ", 8);
  le32(wav + 16, 16);
  le16(wav + 20, 1);
  le16(wav + 22, 1);
  le32(wav + 24, VOICE_RATE);
  le32(wav + 28, VOICE_RATE * 2);
  le16(wav + 32, 2);
  le16(wav + 34, 16);
  memcpy(wav + 36, "data", 4);
  le32(wav + 40, dataBytes);
}

void savePrefs() {
  prefs.putString("ssid", wifiSsid);
  prefs.putString("wifiPass", wifiPass);
  prefs.putString("hub", hubBase);
  prefs.putString("token", token);
  prefs.putInt("volume", volumeLevel);
  prefs.putInt("character", characterIndex);
}

void haptic(uint16_t ms = 30, uint8_t level = 120) {
#if WISP_ENABLE_HAPTICS
  M5.Power.setVibration(level);
  delay(ms);
  M5.Power.setVibration(0);
#else
  (void)ms;
  (void)level;
#endif
}

void text(int x, int y, const String& s, uint16_t color = C_TEXT, float size = 1.0f, uint16_t bg = C_BG) {
  M5.Display.setTextColor(color, bg);
  M5.Display.setTextSize(size);
  M5.Display.setCursor(x, y);
  M5.Display.print(s);
}

void wrapText(const String& s, int x, int y, int maxChars, int maxLines, uint16_t color = C_TEXT, float size = 1.0f, uint16_t bg = C_BG) {
  String line;
  int lineNo = 0;
  int startY = y;
  for (size_t i = 0; i <= s.length(); i++) {
    char c = i < s.length() ? s[i] : ' ';
    bool flush = c == '\n' || line.length() >= maxChars || i == s.length();
    if (!flush) {
      line += c;
      continue;
    }
    line.trim();
    if (line.length()) {
      text(x, startY + lineNo * 18, line, color, size, bg);
      lineNo++;
      if (lineNo >= maxLines) return;
    }
    line = "";
    if (c != '\n' && i < s.length()) line += c;
  }
}

void button(int x, int y, int w, int h, const String& label, uint16_t accent, bool filled = true) {
  uint16_t fill = filled ? C_PANEL2 : C_BG;
  M5.Display.fillRoundRect(x, y, w, h, 10, fill);
  M5.Display.drawRoundRect(x, y, w, h, 10, accent);
  M5.Display.setTextColor(C_TEXT, fill);
  M5.Display.setTextSize(h >= 44 ? 1.55f : 1.15f);
  int tx = x + 10;
  int ty = y + (h >= 44 ? 14 : 9);
  M5.Display.setCursor(tx, ty);
  M5.Display.print(label.substring(0, w / 12));
}

void header(const String& title, uint16_t accent) {
  M5.Display.fillScreen(C_BG);
  M5.Display.fillRect(0, 0, W, HEADER_H, 0x0000);
  M5.Display.fillRect(0, HEADER_H - 3, W, 3, accent);
  text(10, 9, title, C_TEXT, 1.45f, 0x0000);
  String state = WiFi.isConnected() ? WiFi.localIP().toString() : "Wi-Fi off";
  text(162, 8, state.substring(0, 14), WiFi.isConnected() ? C_GREEN : C_AMBER, 0.85f, 0x0000);
  M5.Display.fillRoundRect(272, 5, 42, 24, 7, C_RED);
  M5.Display.drawRoundRect(272, 5, 42, 24, 7, C_AMBER);
  text(280, 12, "Pwr", C_TEXT, 0.85f, C_RED);
}

void nav() {
  for (int i = 0; i < SCREEN_COUNT; i++) {
    Btn b = navBtns[i];
    bool sel = i == screen;
    uint16_t fill = sel ? C_BLUE : C_PANEL;
    M5.Display.fillRoundRect(b.x + 2, b.y + 2, b.w - 4, b.h - 5, 8, fill);
    M5.Display.drawRoundRect(b.x + 2, b.y + 2, b.w - 4, b.h - 5, 8, sel ? C_CYAN : 0x39E7);
    text(b.x + (strlen(b.label) <= 3 ? 19 : 10), b.y + 15, b.label, C_TEXT, 1.05f, fill);
  }
}

void drawSynraPortrait(int x, int y, uint16_t frameColor, const String& label = "") {
  uint16_t hair = 0xF986;
  uint16_t dress = 0x1814;
  uint16_t trim = 0xFFE0;
  if (characterIndex == 1) {
    hair = 0x07FF;
    dress = 0x18E3;
    trim = 0x5D9F;
  } else if (characterIndex == 2) {
    hair = 0xF800;
    dress = 0x4008;
    trim = 0xF81F;
  }

  M5.Display.fillRoundRect(x - 8, y - 8, SYNRA_AVATAR_W + 16, SYNRA_AVATAR_H + 16, 12, C_BG);
  M5.Display.drawRoundRect(x - 8, y - 8, SYNRA_AVATAR_W + 16, SYNRA_AVATAR_H + 16, 12, frameColor);

  // Clean Synra Core2 avatar drawn from primitives; no old bitmap mascot asset.
  M5.Display.fillEllipse(x + 32, y + 24, 27, 24, hair);
  M5.Display.fillRoundRect(x + 8, y + 18, 48, 54, 18, hair);
  M5.Display.fillCircle(x + 32, y + 31, 20, 0xFEF5);
  M5.Display.fillTriangle(x + 6, y + 18, x + 22, y + 8, x + 24, y + 35, hair);
  M5.Display.fillTriangle(x + 58, y + 18, x + 42, y + 8, x + 40, y + 35, hair);
  M5.Display.fillRect(x + 20, y + 20, 24, 5, hair);
  M5.Display.fillCircle(x + 24, y + 33, 3, C_CYAN);
  M5.Display.fillCircle(x + 40, y + 33, 3, C_CYAN);
  M5.Display.drawFastHLine(x + 28, y + 45, 9, 0xC986);
  M5.Display.fillRoundRect(x + 20, y + 55, 24, 11, 5, 0xFEF5);
  M5.Display.fillTriangle(x + 32, y + 62, x + 10, y + 116, x + 54, y + 116, dress);
  M5.Display.drawLine(x + 32, y + 62, x + 32, y + 116, trim);
  M5.Display.drawLine(x + 19, y + 82, x + 45, y + 82, trim);
  M5.Display.fillCircle(x + 32, y + 78, 6, trim);
  M5.Display.drawLine(x + 21, y + 66, x + 8, y + 93, 0xFEF5);
  M5.Display.drawLine(x + 43, y + 66, x + 56, y + 93, 0xFEF5);
  M5.Display.drawLine(x + 22, y + 116, x + 20, y + 132, 0xFEF5);
  M5.Display.drawLine(x + 42, y + 116, x + 44, y + 132, 0xFEF5);
  M5.Display.fillRoundRect(x + 14, y + 130, 13, 4, 2, trim);
  M5.Display.fillRoundRect(x + 38, y + 130, 13, 4, 2, trim);

  if (recording && (millis() / 250) % 2 == 0) {
    M5.Display.drawCircle(x + 32, y + 32, 28, C_PINK);
  }

  if (label.length()) {
    text(x - 5, y + SYNRA_AVATAR_H + 8, label.substring(0, 16), C_TEXT, 0.85f, C_BG);
  }
}

void showMessage(const String& title, const String& body, uint16_t accent) {
  header(title, accent);
  M5.Display.fillRoundRect(14, 50, 292, 136, 14, C_PANEL);
  M5.Display.drawRoundRect(14, 50, 292, 136, 14, accent);
  wrapText(body, 28, 68, 24, 5, C_TEXT, 1.35f, C_PANEL);
  nav();
  dirty = false;
}

void splash() {
  M5.Display.fillScreen(0x0000);
  for (int y = 0; y < H; y++) {
    M5.Display.drawFastHLine(0, y, W, M5.Display.color565(1, 8 + y / 10, 24 + y / 4));
  }
  M5.Display.fillRoundRect(20, 12, 280, 216, 18, C_BG);
  M5.Display.drawRoundRect(20, 12, 280, 216, 18, C_CYAN);
  drawSynraPortrait((W - SYNRA_AVATAR_W) / 2, 21, C_PINK);
  text(47, 164, "NodeSpark Synra", C_TEXT, 1.75f, C_BG);
  text(75, 190, "Core2 Voice Remote", C_CYAN, 1.15f, C_BG);
}

bool beginMic() {
#if WISP_ENABLE_MIC
  if (micReady) return true;
  micReady = M5.Mic.begin();
  return micReady;
#else
  micReady = false;
  return false;
#endif
}

bool beginSpeaker() {
#if WISP_ENABLE_SPEAKER
  if (speakerReady) return true;
  speakerReady = M5.Speaker.begin();
  M5.Speaker.setVolume((uint8_t)constrain(volumeLevel, 0, 255));
  return speakerReady;
#else
  speakerReady = false;
  return false;
#endif
}

void playChime(uint16_t accent) {
#if WISP_ENABLE_SPEAKER
  beginSpeaker();
  M5.Speaker.setVolume((uint8_t)constrain(volumeLevel, 0, 255));
  M5.Speaker.tone(accent == C_RED ? 220 : 740, 65);
  delay(75);
  M5.Speaker.tone(accent == C_RED ? 165 : 980, 80);
#else
  (void)accent;
#endif
}

bool emergencyPowerOrStop(bool allowStopOnly) {
  M5.update();
  if (M5.BtnPWR.wasClicked() || M5.BtnPWR.wasHold() || M5.BtnPWR.pressedFor(900)) {
    M5.Speaker.stop();
    M5.Power.setVibration(0);
    M5.Display.setBrightness(180);
    showMessage("Power Off", "Shutting down Core2.", C_AMBER);
    delay(350);
    M5.Power.powerOff();
    return true;
  }
  if (allowStopOnly && (M5.BtnB.wasHold() || M5.BtnB.pressedFor(1500))) {
    M5.Speaker.stop();
    lastStatus = "Playback stopped";
    dirty = true;
    return true;
  }
  return false;
}

void checkSd() {
#if WISP_ENABLE_SD
  if (!sdReady) sdReady = SD.begin(4);
  if (sdReady) {
    File f = SD.open("/nodespark-core2.log", FILE_APPEND);
    if (f) {
      f.printf("%lu %s %s\n", millis(), deviceId.c_str(), lastStatus.c_str());
      f.close();
    }
  }
#else
  sdReady = false;
#endif
}

HttpResult httpJson(const String& method, const String& path, const String& body = "") {
  HttpResult r;
  if (!WiFi.isConnected()) {
    r.error = "Wi-Fi offline";
    return r;
  }
  String url = path.startsWith("http") ? path : trimBase(hubBase) + path;
  HTTPClient http;
  http.setTimeout(WISP_HTTP_TIMEOUT_MS);
  WiFiClient plain;
  WiFiClientSecure secure;
  bool ok = false;
  if (url.startsWith("https://")) {
    secure.setInsecure();
    ok = http.begin(secure, url);
  } else {
    ok = http.begin(plain, url);
  }
  if (!ok) {
    r.error = "HTTP begin failed";
    return r;
  }
  http.addHeader("Accept", "application/json");
  http.addHeader("User-Agent", APP_VERSION);
  http.addHeader("X-NodeSparkHub-Device-ID", deviceId);
  http.addHeader("X-NodeSparkHub-Device-Name", deviceName);
  if (token.length()) {
    http.addHeader("Authorization", "Bearer " + token);
    http.addHeader("X-NodeSpark-Token", token);
  }
  if (method == "POST") {
    http.addHeader("Content-Type", "application/json");
    r.status = http.POST(body);
  } else {
    r.status = http.GET();
  }
  r.body = http.getString();
  if (r.status < 200 || r.status >= 300) {
    r.error = r.status < 0 ? http.errorToString(r.status) : "HTTP " + String(r.status);
  }
  http.end();
  return r;
}

HttpResult httpAudio(const String& path, const uint8_t* data, size_t len) {
  HttpResult r;
  if (!WiFi.isConnected()) {
    r.error = "Wi-Fi offline";
    return r;
  }
  String url = trimBase(hubBase) + path;
  HTTPClient http;
  http.setTimeout(25000);
  WiFiClient plain;
  WiFiClientSecure secure;
  bool ok = false;
  if (url.startsWith("https://")) {
    secure.setInsecure();
    ok = http.begin(secure, url);
  } else {
    ok = http.begin(plain, url);
  }
  if (!ok) {
    r.error = "HTTP begin failed";
    return r;
  }
  http.addHeader("Content-Type", "audio/wav");
  http.addHeader("Accept", "application/json");
  http.addHeader("User-Agent", APP_VERSION);
  http.addHeader("X-NodeSparkHub-Device-ID", deviceId);
  if (token.length()) http.addHeader("Authorization", "Bearer " + token);
  r.status = http.POST((uint8_t*)data, len);
  r.body = http.getString();
  if (r.status < 200 || r.status >= 300) {
    r.error = r.status < 0 ? http.errorToString(r.status) : "HTTP " + String(r.status);
  }
  http.end();
  return r;
}

bool fetchBinary(const String& path, uint8_t** out, size_t* outLen) {
  *out = nullptr;
  *outLen = 0;
  if (!WiFi.isConnected()) return false;
  String url = path.startsWith("http") ? path : trimBase(hubBase) + path;
  HTTPClient http;
  http.setTimeout(20000);
  WiFiClient plain;
  WiFiClientSecure secure;
  bool ok = false;
  if (url.startsWith("https://")) {
    secure.setInsecure();
    ok = http.begin(secure, url);
  } else {
    ok = http.begin(plain, url);
  }
  if (!ok) return false;
  if (token.length()) http.addHeader("Authorization", "Bearer " + token);
  int code = http.GET();
  if (code != 200) {
    http.end();
    return false;
  }
  int len = http.getSize();
  if (len <= 0 || len > 2 * 1024 * 1024) {
    http.end();
    return false;
  }
  uint8_t* data = (uint8_t*)ps_malloc(len);
  if (!data) data = (uint8_t*)malloc(len);
  if (!data) {
    http.end();
    return false;
  }
  WiFiClient* stream = http.getStreamPtr();
  size_t pos = 0;
  uint32_t start = millis();
  while (pos < (size_t)len && millis() - start < 20000) {
    size_t avail = stream->available();
    if (avail) {
      int got = stream->readBytes(data + pos, min(avail, (size_t)len - pos));
      if (got > 0) pos += got;
    } else {
      delay(4);
    }
  }
  http.end();
  if (pos != (size_t)len) {
    free(data);
    return false;
  }
  *out = data;
  *outLen = pos;
  return true;
}

uint16_t readU16(const uint8_t* p) {
  return (uint16_t)p[0] | ((uint16_t)p[1] << 8);
}

uint32_t readU32(const uint8_t* p) {
  return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

bool playPcmWav(uint8_t* wav, size_t wavLen) {
  if (!wav || wavLen < 44) {
    lastStatus = "Speech WAV is empty";
    return false;
  }
  if (memcmp(wav, "RIFF", 4) != 0 || memcmp(wav + 8, "WAVE", 4) != 0) {
    lastStatus = "Speech is not a WAV file";
    return false;
  }

  uint16_t audioFormat = 0;
  uint16_t channels = 0;
  uint32_t sampleRate = 0;
  uint16_t bits = 0;
  uint8_t* data = nullptr;
  uint32_t dataBytes = 0;

  size_t pos = 12;
  while (pos + 8 <= wavLen) {
    const uint8_t* chunk = wav + pos;
    uint32_t chunkSize = readU32(chunk + 4);
    size_t payload = pos + 8;
    if (payload + chunkSize > wavLen) break;

    if (memcmp(chunk, "fmt ", 4) == 0 && chunkSize >= 16) {
      audioFormat = readU16(wav + payload);
      channels = readU16(wav + payload + 2);
      sampleRate = readU32(wav + payload + 4);
      bits = readU16(wav + payload + 14);
    } else if (memcmp(chunk, "data", 4) == 0) {
      data = wav + payload;
      dataBytes = chunkSize;
    }
    pos = payload + chunkSize + (chunkSize & 1);
  }

  if (!data || !dataBytes || audioFormat != 1 || sampleRate < 8000 || sampleRate > 48000) {
    lastStatus = "Unsupported speech WAV";
    return false;
  }
  if (channels != 1 && channels != 2) {
    lastStatus = "Unsupported speech channels";
    return false;
  }
  if (bits != 16 && bits != 8) {
    lastStatus = "Unsupported speech bits";
    return false;
  }

  M5.Speaker.stop();
  M5.Speaker.setVolume((uint8_t)constrain(volumeLevel, 0, 255));
  bool ok = false;
  int16_t* mono16 = nullptr;
  uint8_t* mono8 = nullptr;
  size_t playbackSamples = 0;

  if (bits == 16) {
    size_t sampleWords = dataBytes / 2;
    if (channels == 1) {
      playbackSamples = sampleWords;
      ok = M5.Speaker.playRaw((const int16_t*)data, sampleWords, sampleRate, false, 1, -1, true);
    } else {
      size_t frames = sampleWords / 2;
      playbackSamples = frames;
      mono16 = (int16_t*)ps_malloc(frames * sizeof(int16_t));
      if (!mono16) mono16 = (int16_t*)malloc(frames * sizeof(int16_t));
      if (!mono16) {
        lastStatus = "No memory for speech mix";
        return false;
      }
      const int16_t* stereo = (const int16_t*)data;
      for (size_t i = 0; i < frames; i++) {
        mono16[i] = (int16_t)(((int32_t)stereo[i * 2] + (int32_t)stereo[i * 2 + 1]) / 2);
      }
      ok = M5.Speaker.playRaw(mono16, frames, sampleRate, false, 1, -1, true);
    }
  } else {
    size_t samples = dataBytes;
    if (channels == 1) {
      playbackSamples = samples;
      ok = M5.Speaker.playRaw((const uint8_t*)data, samples, sampleRate, false, 1, -1, true);
    } else {
      size_t frames = samples / 2;
      playbackSamples = frames;
      mono8 = (uint8_t*)ps_malloc(frames);
      if (!mono8) mono8 = (uint8_t*)malloc(frames);
      if (!mono8) {
        lastStatus = "No memory for speech mix";
        return false;
      }
      for (size_t i = 0; i < frames; i++) {
        mono8[i] = (uint8_t)(((uint16_t)data[i * 2] + (uint16_t)data[i * 2 + 1]) / 2);
      }
      ok = M5.Speaker.playRaw(mono8, frames, sampleRate, false, 1, -1, true);
    }
  }

  uint32_t start = millis();
  uint32_t maxPlayMs = playbackSamples && sampleRate
    ? min<uint32_t>(30000, (uint32_t)((playbackSamples * 1000ULL) / sampleRate) + 1800)
    : 8000;
  while (ok && M5.Speaker.isPlaying() && millis() - start < maxPlayMs) {
    if (emergencyPowerOrStop(true)) {
      ok = false;
      break;
    }
    delay(15);
  }
  M5.Speaker.stop();
  if (mono16) free(mono16);
  if (mono8) free(mono8);
  if (!ok) lastStatus = "Speaker refused speech audio";
  return ok;
}

bool playSpeechPath(const String& path) {
  if (!path.length() || !beginSpeaker()) return false;
  uint8_t* wav = nullptr;
  size_t wavLen = 0;
  showMessage("Speaking", "Playing voice reply. Hold middle button to stop, or side power to shut down.", C_PINK);
  if (!fetchBinary(path, &wav, &wavLen)) {
    lastStatus = "Speech download failed";
    return false;
  }
  bool ok = playPcmWav(wav, wavLen);
  free(wav);
  return ok;
}

bool connectWifi(bool show = true) {
  if (WiFi.isConnected()) return true;
  if (!wifiSsid.length()) {
    lastStatus = "Set Wi-Fi in Setup";
    return false;
  }
  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);
  WiFi.begin(wifiSsid.c_str(), wifiPass.c_str());
  uint32_t start = millis();
  if (show) showMessage("Wi-Fi", "Connecting to " + wifiSsid + "...", C_BLUE);
  while (!WiFi.isConnected() && millis() - start < 18000) {
    M5.update();
    delay(100);
  }
  if (WiFi.isConnected()) {
    lastStatus = "Wi-Fi connected";
    return true;
  }
  lastStatus = "Wi-Fi failed";
  return false;
}

bool healthCheck() {
  HttpResult r = httpJson("GET", "/health");
  hubOnline = r.status >= 200 && r.status < 300;
  lastStatus = hubOnline ? "Hub online" : (r.error.length() ? r.error : "Hub offline");
  return hubOnline;
}

bool checkIn() {
  String body = "{";
  body += "\"deviceId\":\"" + jsonEscape(deviceId) + "\",";
  body += "\"name\":\"" + jsonEscape(deviceName) + "\",";
  body += "\"platform\":\"M5Stack Core2 / NodeSpark Synra Voice Remote\",";
  body += "\"osVersion\":\"ESP32 Arduino\",";
  body += "\"appVersion\":\"" + String(APP_VERSION) + "\",";
  body += "\"firmwareVersion\":\"" + String(APP_VERSION) + "\",";
  body += "\"characterName\":\"" + jsonEscape(selectedCharacterName()) + "\",";
  body += "\"batteryPercent\":" + String(M5.Power.getBatteryLevel()) + ",";
  body += "\"isCharging\":" + String(M5.Power.isCharging() ? "true" : "false") + ",";
  body += "\"wifiSSID\":\"" + jsonEscape(WiFi.isConnected() ? WiFi.SSID() : String("")) + "\",";
  body += "\"wifiRSSI\":" + String(WiFi.isConnected() ? WiFi.RSSI() : 0) + ",";
  body += "\"ipAddress\":\"" + jsonEscape(WiFi.isConnected() ? WiFi.localIP().toString() : String("")) + "\",";
  body += "\"audioReady\":" + String((micReady && speakerReady) ? "true" : "false") + ",";
  body += "\"micReady\":" + String(micReady ? "true" : "false") + ",";
  body += "\"speakerReady\":" + String(speakerReady ? "true" : "false") + ",";
  body += "\"sdReady\":" + String(sdReady ? "true" : "false") + ",";
  body += "\"uptimeSeconds\":" + String(millis() / 1000) + ",";
  body += "\"lastStatus\":\"" + jsonEscape(lastStatus) + "\",";
  body += "\"capabilities\":[\"display\",\"touch\",\"speaker\",\"microphone\",\"button\",\"haptic\",\"battery\",\"sd\",\"assistant\",\"voiceOnly\",\"deviceCommands\",\"bluetooth\"]";
  body += "}";
  HttpResult r = httpJson("POST", "/devices/checkin", body);
  bool ok = r.status >= 200 && r.status < 300;
  if (ok) lastStatus = "Checked in";
  else lastStatus = r.error.length() ? r.error : "Check-in failed";
  return ok;
}

bool pairWithCode() {
  pairCode.trim();
  if (!pairCode.length()) {
    showMessage("Pair", "Enter the pairing code shown in NodeSparkHub.", C_AMBER);
    return false;
  }
  if (!WiFi.isConnected() && !connectWifi(true)) {
    showMessage("Pair", "Wi-Fi is not connected. Open Setup first.", C_AMBER);
    return false;
  }
  String body = "{";
  body += "\"code\":\"" + jsonEscape(pairCode) + "\",";
  body += "\"deviceId\":\"" + jsonEscape(deviceId) + "\",";
  body += "\"deviceName\":\"" + jsonEscape(deviceName) + "\",";
  body += "\"platform\":\"M5Stack Core2 / NodeSpark Synra Voice Remote\",";
  body += "\"osVersion\":\"ESP32 Arduino\",";
  body += "\"appVersion\":\"" + String(APP_VERSION) + "\"";
  body += "}";
  showMessage("Pair", "Pairing with NodeSparkHub...", C_AMBER);
  HttpResult r = httpJson("POST", "/pair", body);
  if (r.status < 200 || r.status >= 300) {
    showMessage("Pair Failed", r.body.length() ? r.body.substring(0, 160) : r.error, C_RED);
    return false;
  }
  DynamicJsonDocument doc(2048);
  if (deserializeJson(doc, r.body)) {
    showMessage("Pair Failed", "Hub returned bad JSON.", C_RED);
    return false;
  }
  token = doc["deviceToken"].as<String>();
  if (!token.length()) {
    showMessage("Pair Failed", "No device token was returned.", C_RED);
    return false;
  }
  savePrefs();
  prefs.remove("pendingPair");
  pairCode = "";
  lastStatus = "Paired with Hub";
  checkIn();
  showMessage("Paired", "Synra Core2 voice remote is ready.", C_GREEN);
  playChime(C_GREEN);
  return true;
}

void askAssistant(const String& prompt) {
  if (!token.length()) {
    showMessage("Pair Required", "Pair Core2 with NodeSparkHub first.", C_AMBER);
    return;
  }
  if (!WiFi.isConnected() && !connectWifi(true)) {
    showMessage("AI Offline", "Wi-Fi is not connected.", C_AMBER);
    return;
  }
  String body = "{";
  body += "\"text\":\"" + jsonEscape(prompt) + "\",";
  body += "\"deviceId\":\"" + jsonEscape(deviceId) + "\",";
  body += "\"deviceName\":\"" + jsonEscape(deviceName) + "\",";
  body += "\"source\":\"synra-core2\",";
  body += "\"platform\":\"M5Stack Core2 / voice-only\",";
  body += "\"productName\":\"NodeSpark Synra Core2\",";
  body += "\"assistantName\":\"Synra\",";
  body += "\"personaName\":\"" + jsonEscape(selectedCharacterName()) + "\",";
  body += "\"characterName\":\"" + jsonEscape(selectedCharacterName()) + "\",";
  body += "\"preferredSpeechFormat\":\"wav\",";
  body += "\"voice\":true,";
  body += "\"capabilities\":[\"display\",\"touch\",\"speaker\",\"microphone\",\"assistant\",\"voiceOnly\",\"noCamera\",\"noTextInput\"]";
  body += "}";
  showMessage("Ask Synra", selectedCharacterName() + " is listening through the Hub...", C_PINK);
  HttpResult r = httpJson("POST", "/synra/assistant", body);
  if (r.status < 200 || r.status >= 300) {
    lastStatus = r.error.length() ? r.error : "AI request failed";
    showMessage("Synra Failed", r.body.length() ? r.body.substring(0, 180) : lastStatus, C_RED);
    return;
  }
  DynamicJsonDocument doc(8192);
  if (deserializeJson(doc, r.body)) {
    showMessage("Synra Failed", "Hub returned bad assistant JSON.", C_RED);
    return;
  }
  lastAssistantReply = doc["displayText"].as<String>();
  if (!lastAssistantReply.length()) lastAssistantReply = doc["reply"].as<String>();
  String speech = doc["speechPath"].as<String>();
  if (!speech.length()) speech = doc["speechURL"].as<String>();
  if (!lastAssistantReply.length()) lastAssistantReply = doc["error"].as<String>();
  lastStatus = doc["ok"].as<bool>() ? "Synra answered" : "Synra returned an error";
  showMessage("Synra", lastAssistantReply.substring(0, 260), doc["ok"].as<bool>() ? C_GREEN : C_AMBER);
  if (speech.length()) playSpeechPath(speech);
}

void drawRecordMeter(uint32_t elapsed, int level) {
  M5.Display.fillRoundRect(24, 135, 272, 30, 8, C_PANEL2);
  M5.Display.drawRoundRect(24, 135, 272, 30, 8, C_PINK);
  M5.Display.fillRoundRect(34, 143, map(constrain(level, 0, 100), 0, 100, 0, 252), 12, 5, C_PINK);
  text(34, 170, "Recording " + String(elapsed / 1000) + "s", C_MUTED, 1.0f, C_BG);
}

void startVoiceAssistant() {
  if (!token.length()) {
    showMessage("Pair Required", "Pair Core2 with NodeSparkHub first.", C_AMBER);
    return;
  }
  if (!WiFi.isConnected() && !connectWifi(true)) {
    showMessage("AI Offline", "Wi-Fi is not connected.", C_AMBER);
    return;
  }
  if (!beginMic()) {
    showMessage("Mic Not Ready", "Core2 microphone did not start. Restart and try Hardware > Mic.", C_RED);
    return;
  }
  size_t sampleCount = (VOICE_RATE * VOICE_MS) / 1000;
  size_t dataBytes = sampleCount * sizeof(int16_t);
  size_t wavBytes = 44 + dataBytes;
  uint8_t* wav = (uint8_t*)ps_malloc(wavBytes);
  if (!wav) wav = (uint8_t*)malloc(wavBytes);
  if (!wav) {
    showMessage("Voice", "Not enough memory for voice recording.", C_RED);
    return;
  }
  writeWavHeader(wav, 0);
  showMessage("Listening", "Speak now. Core2 will auto-send.", C_PINK);
  haptic(45, 150);
  recording = true;
  uint32_t start = millis();
  uint32_t lastMeter = 0;
  size_t pos = 44;
  static int16_t samples[320];
  while (millis() - start < VOICE_MS && pos + sizeof(samples) <= wavBytes) {
    M5.update();
    if (M5.Mic.record(samples, 320, VOICE_RATE)) {
      memcpy(wav + pos, samples, sizeof(samples));
      pos += sizeof(samples);
      if (millis() - lastMeter > 160) {
        uint32_t total = 0;
        for (int i = 0; i < 320; i++) total += abs(samples[i]);
        micLevel = constrain((int)(total / 320 / 70), 0, 100);
        drawRecordMeter(millis() - start, micLevel);
        lastMeter = millis();
      }
    } else {
      delay(5);
    }
  }
  recording = false;
  uint32_t actualData = pos > 44 ? pos - 44 : 0;
  writeWavHeader(wav, actualData);
  if (actualData < 2400) {
    free(wav);
    showMessage("No Speech", "Core2 did not capture enough audio.", C_AMBER);
    return;
  }
  showMessage("Transcribing", "Sending speech to NodeSparkHub...", C_PINK);
  HttpResult t = httpAudio("/synra/transcribe", wav, pos);
  free(wav);
  if (t.status < 200 || t.status >= 300) {
    showMessage("Transcribe Failed", t.body.length() ? t.body.substring(0, 180) : t.error, C_RED);
    return;
  }
  DynamicJsonDocument doc(4096);
  if (deserializeJson(doc, t.body)) {
    showMessage("Transcribe Failed", "Hub returned bad transcription JSON.", C_RED);
    return;
  }
  lastTranscript = doc["text"].as<String>();
  lastTranscript.trim();
  if (!lastTranscript.length()) {
    String err = doc["error"].as<String>();
    showMessage("No Speech", err.length() ? err : "NodeSparkHub did not detect speech.", C_AMBER);
    return;
  }
  showMessage("You Said", lastTranscript.substring(0, 220), C_CYAN);
  delay(700);
  askAssistant(lastTranscript);
}

void runWorkflow() {
  showMessage("Voice Remote", "Synra Core2 is voice-only. Ask Synra to explain or prepare actions; live workflow runs stay confirm-gated on the Hub.", C_AMBER);
}

void ackCommand(const String& id, const String& status, const String& result) {
  if (!id.length()) return;
  String body = "{\"status\":\"" + jsonEscape(status) + "\",\"result\":\"" + jsonEscape(result) + "\"}";
  httpJson("POST", "/devices/" + deviceId + "/commands/" + id + "/ack", body);
}

void speakTextViaHub(const String& bodyText) {
  String body = "{";
  body += "\"text\":\"" + jsonEscape(bodyText) + "\",";
  body += "\"deviceId\":\"" + jsonEscape(deviceId) + "\",";
  body += "\"deviceName\":\"" + jsonEscape(deviceName) + "\",";
  body += "\"source\":\"synra-core2-speak\",";
  body += "\"preferredSpeechFormat\":\"wav\",";
  body += "\"voice\":true";
  body += "}";
  HttpResult r = httpJson("POST", "/synra/speech", body);
  if (r.status >= 200 && r.status < 300) {
    DynamicJsonDocument doc(2048);
    if (!deserializeJson(doc, r.body)) {
      String speech = doc["speechPath"].as<String>();
      if (!speech.length()) speech = doc["speechURL"].as<String>();
      if (speech.length()) {
        playSpeechPath(speech);
        return;
      }
    }
  }
  playChime(C_AMBER);
}

void handleCommand(JsonObjectConst c) {
  String id = c["id"].as<String>();
  String kind = c["type"].as<String>();
  kind.toLowerCase();
  String title = c["title"].as<String>();
  String body = c["body"].as<String>();
  if (!body.length()) body = c["text"].as<String>();
  if (!body.length()) body = c["detail"].as<String>();
  String speech = c["speechPath"].as<String>();
  if (!speech.length()) speech = c["speechURL"].as<String>();
  if (!speech.length() && c["payload"].is<JsonObjectConst>()) {
    speech = c["payload"]["speechPath"].as<String>();
    if (!speech.length()) speech = c["payload"]["speechURL"].as<String>();
  }
  lastCommand = kind + ": " + (title.length() ? title : body);

  if (kind == "speak" || kind == "say" || kind == "speech" || kind == "tts" || kind == "speaker") {
    showMessage(title.length() ? title : "Speak", body.length() ? body : "NodeSpark Synra is ready.", C_PINK);
    if (speech.length()) playSpeechPath(speech);
    else speakTextViaHub(body.length() ? body : "NodeSpark Synra is ready.");
    ackCommand(id, "completed", "spoken");
  } else if (kind == "sound" || kind == "chime") {
    showMessage(title.length() ? title : "Sound", body.length() ? body : "Sound command received.", C_CYAN);
    playChime(C_CYAN);
    ackCommand(id, "completed", "played");
  } else if (kind == "assistant" || kind == "ask" || kind == "askai") {
    askAssistant(body.length() ? body : "Help me from NodeSpark Synra Core2.");
    ackCommand(id, "completed", "assistant answered");
  } else if (kind == "workflow" || kind == "runworkflow") {
    runWorkflow();
    ackCommand(id, "denied", "Synra Core2 is voice-only; workflow execution stays confirm-gated on Hub.");
  } else if (kind == "volume") {
    int v = c["volume"] | c["percent"] | c["value"] | volumeLevel;
    if (v <= 100) v = map(v, 0, 100, 0, 255);
    volumeLevel = constrain(v, 0, 255);
    savePrefs();
    showMessage("Volume", "Volume set to " + String(map(volumeLevel, 0, 255, 0, 100)) + "%", C_CYAN);
    ackCommand(id, "completed", "volume set");
  } else if (kind == "display" || kind == "message" || kind == "demo" || kind == "card" || kind == "ping") {
    showMessage(title.length() ? title : "NodeSparkHub", body.length() ? body : "Command received.", C_GREEN);
    ackCommand(id, "completed", "shown");
  } else {
    showMessage(title.length() ? title : "Command", body.length() ? body : kind, C_BLUE);
    ackCommand(id, "completed", "received");
  }
}

void pollCommands() {
  if (!token.length() || !WiFi.isConnected()) return;
  HttpResult r = httpJson("GET", "/devices/" + deviceId + "/commands/poll?limit=5");
  if (r.status < 200 || r.status >= 300) {
    lastStatus = r.error.length() ? r.error : "Poll failed";
    return;
  }
  DynamicJsonDocument doc(8192);
  if (deserializeJson(doc, r.body)) return;
  JsonArrayConst commands = doc["commands"].as<JsonArrayConst>();
  for (JsonObjectConst c : commands) handleCommand(c);
}

String portalHtml() {
  String networks;
  int n = WiFi.scanNetworks(false, true);
  if (n > 0) {
    networks += F("<label>Nearby Wi-Fi</label><select onchange=\"document.getElementById('ssid').value=this.value\"><option value=''>Choose network...</option>");
    for (int i = 0; i < n && i < 18; i++) {
      String s = WiFi.SSID(i);
      networks += "<option value=\"" + htmlEscape(s) + "\">" + htmlEscape(s) + " (" + String(WiFi.RSSI(i)) + ")</option>";
    }
    networks += F("</select>");
  }
  String html = F("<!doctype html><html><head><meta name='viewport' content='width=device-width,initial-scale=1'>"
                  "<title>NodeSpark Synra Core2 Setup</title><style>"
                  "body{font-family:-apple-system,BlinkMacSystemFont,'Segoe UI',sans-serif;background:#07131f;color:white;margin:0}"
                  ".wrap{max-width:560px;margin:auto;padding:22px}.logo{font-size:30px;font-weight:800;color:#61f7ff}"
                  ".card{background:#102438;border:1px solid #25627d;border-radius:18px;padding:18px;margin-top:18px}"
                  "label{display:block;margin:14px 0 6px;color:#c9d7e5;font-weight:700}"
                  "input,select{width:100%;box-sizing:border-box;border:1px solid #34718f;border-radius:12px;padding:13px;background:#06111e;color:white;font-size:17px}"
                  "button{margin-top:18px;width:100%;border:0;border-radius:13px;padding:14px;font-weight:800;font-size:17px;background:#14e7ff;color:#001018}"
                  ".hint{color:#9db3c6;font-size:14px;line-height:1.4}</style></head><body><div class='wrap'><div class='logo'>NodeSpark Synra Core2</div>"
                  "<div class='hint'>Enter your Wi-Fi and NodeSparkHub address. Cloudflare URLs like https://nodespark.msidragon.com work here.</div>"
                  "<form method='POST' action='/save'><div class='card'>");
  html += networks;
  html += "<label>Wi-Fi name</label><input id='ssid' name='ssid' value='" + htmlEscape(wifiSsid) + "'>";
  html += "<label>Wi-Fi password</label><input name='pass' type='password' value='" + htmlEscape(wifiPass) + "'>";
  html += "<label>NodeSparkHub URL</label><input name='hub' value='" + htmlEscape(hubBase) + "'>";
  html += "<label>Hub pairing code</label><input name='pair' inputmode='numeric' pattern='[0-9]*' placeholder='6-digit code from NodeSparkHub' value='" + htmlEscape(pairCode) + "'>";
  html += "<label>Synra character</label><select name='character'>";
  for (int i = 0; i < SYNRA_CHARACTER_COUNT; i++) {
    html += "<option value='" + String(i) + "'";
    if (i == characterIndex) html += " selected";
    html += ">" + htmlEscape(String(SYNRA_CHARACTERS[i])) + "</option>";
  }
  html += "</select>";
  html += F("<button>Save and restart</button></div></form></div></body></html>");
  return html;
}

void stopPortal() {
  if (!portalActive) return;
  portal.stop();
  dns.stop();
  WiFi.softAPdisconnect(true);
  portalActive = false;
}

void startPortal() {
  portalApName = "NodeSpark-Core2-" + deviceId.substring(deviceId.length() - 4);
  WiFi.mode(WIFI_AP_STA);
  WiFi.softAP(portalApName.c_str());
  dns.start(DNS_PORT, "*", WiFi.softAPIP());
  portal.on("/", HTTP_GET, []() { portal.send(200, "text/html", portalHtml()); });
  portal.on("/save", HTTP_POST, []() {
    wifiSsid = portal.arg("ssid");
    wifiPass = portal.arg("pass");
    hubBase = trimBase(portal.arg("hub"));
    pairCode = portal.arg("pair");
    pairCode.trim();
    characterIndex = constrain(portal.arg("character").toInt(), 0, SYNRA_CHARACTER_COUNT - 1);
    savePrefs();
    prefs.putString("pendingPair", pairCode);
    portal.send(200, "text/html", "<html><body><h2>Saved. Core2 is restarting.</h2></body></html>");
    delay(600);
    ESP.restart();
  });
  portal.onNotFound([]() { portal.send(200, "text/html", portalHtml()); });
  portal.begin();
  portalActive = true;
  showMessage("Wi-Fi Setup", "Connect phone/computer to " + portalApName + " then open 192.168.4.1", C_GREEN);
}

void drawHome() {
  header("NodeSpark Synra", hubOnline ? C_GREEN : C_CYAN);
  M5.Display.fillRoundRect(12, 46, 296, 144, 14, C_PANEL);
  M5.Display.drawRoundRect(12, 46, 296, 144, 14, hubOnline ? C_GREEN : C_BLUE);
  drawSynraPortrait(226, 51, hubOnline ? C_GREEN : C_PINK);
  text(26, 58, selectedCharacterName(), C_PINK, 1.45f, C_PANEL);
  text(26, 84, WiFi.isConnected() ? "Wi-Fi connected" : "Wi-Fi not connected", WiFi.isConnected() ? C_GREEN : C_AMBER, 1.15f, C_PANEL);
  text(26, 108, hubOnline ? "Hub online" : "Hub not checked", hubOnline ? C_GREEN : C_MUTED, 1.15f, C_PANEL);
  text(26, 132, token.length() ? "Paired to Hub" : "Not paired", token.length() ? C_GREEN : C_AMBER, 1.15f, C_PANEL);
  wrapText(lastStatus, 26, 155, 24, 1, C_MUTED, 0.9f, C_PANEL);
  button(132, 150, 82, 30, "Ping", C_BLUE);
  nav();
}

void drawAI() {
  header("Synra Voice", C_PINK);
  drawSynraPortrait(232, 49, C_PINK);
  button(18, 50, 196, 52, "Tap to Talk", C_PINK);
  button(18, 112, 96, 44, "Ask Demo", C_CYAN);
  button(122, 112, 92, 44, "Character", C_GREEN);
  String info = lastTranscript.length() ? "You: " + lastTranscript : "Voice-only remote. Mic + speaker use Hub Synra and selected Hub voice.";
  wrapText(info, 20, 166, 28, 2, C_MUTED, 1.0f, C_BG);
  nav();
}

void drawPair() {
  header("Pair Core2", C_AMBER);
  text(18, 45, "Enter Hub pairing code", C_MUTED, 1.15f);
  M5.Display.fillRoundRect(18, 66, 284, 34, 8, C_PANEL);
  text(32, 75, pairCode.length() ? pairCode : "Code", pairCode.length() ? C_TEXT : C_MUTED, 1.45f, C_PANEL);
  int x0 = 18;
  int y0 = 108;
  for (int i = 0; i < 10; i++) {
    button(x0 + (i % 5) * 45, y0 + (i / 5) * 36, 38, 30, String(i), C_BLUE);
  }
  button(250, 108, 54, 30, "Del", C_AMBER);
  button(250, 144, 54, 30, "Go", C_GREEN);
  nav();
}

void drawHardware() {
  header("Hardware", C_GREEN);
  text(18, 48, "Mic", micReady ? C_GREEN : C_AMBER, 1.25f);
  M5.Display.drawRoundRect(70, 48, 232, 20, 6, C_CYAN);
  M5.Display.fillRoundRect(72, 50, map(micLevel, 0, 100, 0, 228), 16, 5, C_CYAN);
  text(18, 78, "Speaker: " + String(speakerReady ? "ready" : "tap test"), C_TEXT, 1.1f);
  text(18, 102, "SD: " + String(sdReady ? "ready" : "not mounted"), sdReady ? C_GREEN : C_AMBER, 1.1f);
  text(18, 126, "Volume: " + String(map(volumeLevel, 0, 255, 0, 100)) + "%", C_TEXT, 1.1f);
  button(18, 158, 88, 34, "Sound", C_CYAN);
  button(116, 158, 88, 34, "Mic", C_GREEN);
  button(214, 158, 88, 34, "SD", sdReady ? C_GREEN : C_AMBER);
  nav();
}

void drawSetup() {
  header("Settings", C_BLUE);
  drawSynraPortrait(232, 48, C_CYAN);
  button(18, 48, 284, 48, "Wi-Fi / Hub Setup", C_GREEN);
  button(18, 106, 62, 34, "Vol -", C_CYAN);
  button(88, 106, 62, 34, "Vol +", C_CYAN);
  button(158, 106, 62, 34, "Restart", C_AMBER);
  button(18, 148, 132, 34, selectedCharacterName(), C_PINK);
  button(158, 148, 62, 34, "Pair", C_AMBER);
  wrapText("Hub: " + hubBase, 18, 185, 30, 1, C_MUTED, 0.95f);
  nav();
}

void drawScreen() {
  dirty = false;
  switch (screen) {
    case SCREEN_HOME: drawHome(); break;
    case SCREEN_AI: drawAI(); break;
    case SCREEN_PAIR: drawPair(); break;
    case SCREEN_HARDWARE: drawHardware(); break;
    case SCREEN_SETUP: drawSetup(); break;
    default: drawHome(); break;
  }
}

bool hit(int x, int y, int bx, int by, int bw, int bh) {
  return x >= bx && x <= bx + bw && y >= by && y <= by + bh;
}

void handleTouchAt(int x, int y) {
  lastUiTouchMs = millis();
  if (y <= HEADER_H && x >= 266) {
    M5.Speaker.stop();
    M5.Power.setVibration(0);
    showMessage("Power Off", "Shutting down Core2.", C_AMBER);
    delay(350);
    M5.Power.powerOff();
    return;
  }
  if (y >= NAV_Y) {
    int idx = constrain(x / 64, 0, SCREEN_COUNT - 1);
    screen = (Screen)idx;
    dirty = true;
    return;
  }
  if (screen == SCREEN_HOME) {
    if (hit(x, y, 132, 150, 82, 30)) {
      healthCheck();
      checkIn();
      dirty = true;
    }
  } else if (screen == SCREEN_AI) {
    if (hit(x, y, 18, 50, 196, 52)) startVoiceAssistant();
    else if (hit(x, y, 18, 112, 96, 44)) askAssistant("Give a short impressive demo of what NodeSpark Synra Core2 can do with NodeSparkHub.");
    else if (hit(x, y, 122, 112, 92, 44)) cycleCharacter();
  } else if (screen == SCREEN_PAIR) {
    for (int i = 0; i < 10; i++) {
      int bx = 18 + (i % 5) * 45;
      int by = 108 + (i / 5) * 36;
      if (hit(x, y, bx, by, 38, 30)) {
        if (pairCode.length() < 12) pairCode += String(i);
        dirty = true;
        return;
      }
    }
    if (hit(x, y, 250, 108, 54, 30)) {
      if (pairCode.length()) pairCode.remove(pairCode.length() - 1);
      dirty = true;
    } else if (hit(x, y, 250, 144, 54, 30)) {
      pairWithCode();
      dirty = true;
    }
  } else if (screen == SCREEN_HARDWARE) {
    if (hit(x, y, 18, 158, 88, 34)) {
      beginSpeaker();
      playChime(C_CYAN);
      dirty = true;
    } else if (hit(x, y, 116, 158, 88, 34)) {
      beginMic();
      dirty = true;
    } else if (hit(x, y, 214, 158, 88, 34)) {
      sdReady = false;
      checkSd();
      dirty = true;
    }
  } else if (screen == SCREEN_SETUP) {
    if (hit(x, y, 18, 48, 284, 48)) {
      startPortal();
    } else if (hit(x, y, 18, 106, 62, 38)) {
      volumeLevel = max(0, volumeLevel - 24);
      savePrefs();
      playChime(C_CYAN);
      dirty = true;
    } else if (hit(x, y, 88, 106, 62, 38)) {
      volumeLevel = min(255, volumeLevel + 24);
      savePrefs();
      playChime(C_CYAN);
      dirty = true;
    } else if (hit(x, y, 158, 106, 62, 38)) {
      showMessage("Restart", "Restarting Core2.", C_AMBER);
      delay(500);
      ESP.restart();
    } else if (hit(x, y, 18, 148, 132, 34)) {
      cycleCharacter();
    } else if (hit(x, y, 158, 148, 62, 34)) {
      screen = SCREEN_PAIR;
      dirty = true;
    }
  }
}

void handleTouch() {
  static uint32_t lastTouchActionMs = 0;
  if (millis() - lastTouchActionMs < 180) return;
  int count = M5.Touch.getCount();
  for (int i = 0; i < count; i++) {
    auto td = M5.Touch.getDetail(i);
    if (td.wasPressed() || td.wasClicked() || td.wasReleased()) {
      lastTouchActionMs = millis();
      handleTouchAt(td.x, td.y);
      return;
    }
  }
}

void handleButtons() {
  if (millis() - lastUiTouchMs < 220) return;
  if (emergencyPowerOrStop(false)) return;
  if (M5.BtnA.wasClicked()) {
    screen = (Screen)((screen + SCREEN_COUNT - 1) % SCREEN_COUNT);
    dirty = true;
  }
  if (M5.BtnC.wasClicked()) {
    screen = (Screen)((screen + 1) % SCREEN_COUNT);
    dirty = true;
  }
  if (M5.BtnB.wasClicked()) {
    if (screen == SCREEN_AI) startVoiceAssistant();
    else if (screen == SCREEN_PAIR) pairWithCode();
    else if (screen == SCREEN_HARDWARE) playChime(C_CYAN);
    else if (screen == SCREEN_SETUP) startPortal();
    else {
      healthCheck();
      checkIn();
      dirty = true;
    }
  }
  if (M5.BtnB.wasHold()) {
    showMessage("Power", "Powering off.", C_AMBER);
    delay(500);
    M5.Power.powerOff();
  }
}

void updateMicMeter() {
  if (recording || screen != SCREEN_HARDWARE || millis() - lastMeterMs < 350) return;
  lastMeterMs = millis();
  if (!micReady) return;
  static int16_t samples[160];
  if (!M5.Mic.record(samples, 160, 16000)) return;
  uint32_t total = 0;
  for (int i = 0; i < 160; i++) total += abs(samples[i]);
  micLevel = constrain((int)(total / 160 / 70), 0, 100);
  M5.Display.fillRect(71, 49, 230, 18, C_BG);
  M5.Display.drawRoundRect(70, 48, 232, 20, 6, C_CYAN);
  M5.Display.fillRoundRect(72, 50, map(micLevel, 0, 100, 0, 228), 16, 5, C_CYAN);
}

#if WISP_ENABLE_BLE
void bleNotifyState() {
  if (!bleReady || !bleState) return;
  String body = "{";
  body += "\"deviceId\":\"" + jsonEscape(deviceId) + "\",";
  body += "\"name\":\"" + jsonEscape(deviceName) + "\",";
  body += "\"wifi\":" + String(WiFi.isConnected() ? "true" : "false") + ",";
  body += "\"paired\":" + String(token.length() ? "true" : "false") + ",";
  body += "\"hub\":\"" + jsonEscape(hubBase) + "\",";
  body += "\"character\":\"" + jsonEscape(selectedCharacterName()) + "\",";
  body += "\"status\":\"" + jsonEscape(lastStatus) + "\"";
  body += "}";
  bleState->setValue(body.c_str());
  bleState->notify();
}

void bleNotifyEvent(const String& type, const String& detail) {
  if (!bleReady || !bleEvent) return;
  String body = "{\"type\":\"" + jsonEscape(type) + "\",\"detail\":\"" + jsonEscape(detail) + "\",\"deviceId\":\"" + jsonEscape(deviceId) + "\"}";
  bleEvent->setValue(body.c_str());
  bleEvent->notify();
}

class BleCommandCallbacks : public BLECharacteristicCallbacks {
  void onWrite(BLECharacteristic* c) override {
    String raw = c->getValue().c_str();
    raw.toCharArray(bleCommandRaw, sizeof(bleCommandRaw));
    hasBleCommand = true;
  }
};

void setupBle() {
  BLEDevice::init("NodeSpark Synra Core2");
  BLEServer* server = BLEDevice::createServer();
  BLEService* service = server->createService(BLE_SERVICE_UUID);
  BLECharacteristic* command = service->createCharacteristic(BLE_COMMAND_UUID, BLECharacteristic::PROPERTY_WRITE | BLECharacteristic::PROPERTY_WRITE_NR);
  command->setCallbacks(new BleCommandCallbacks());
  bleEvent = service->createCharacteristic(BLE_EVENT_UUID, BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_NOTIFY);
  bleEvent->addDescriptor(new BLE2902());
  bleState = service->createCharacteristic(BLE_STATE_UUID, BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_NOTIFY);
  bleState->addDescriptor(new BLE2902());
  service->start();
  BLEAdvertising* adv = BLEDevice::getAdvertising();
  adv->addServiceUUID(BLE_SERVICE_UUID);
  adv->setScanResponse(true);
  BLEDevice::startAdvertising();
  bleReady = true;
  bleNotifyState();
}

void processBleCommand() {
  if (!hasBleCommand) return;
  hasBleCommand = false;
  DynamicJsonDocument doc(1024);
  if (deserializeJson(doc, bleCommandRaw)) {
    bleNotifyEvent("error", "Bad BLE JSON");
    return;
  }
  String type = doc["type"].as<String>();
  String textValue = doc["text"].as<String>();
  type.toLowerCase();
  if (type == "assistant" || type == "ask") askAssistant(textValue.length() ? textValue : "Help me from NodeSpark iPhone.");
  else if (type == "speak") speakTextViaHub(textValue.length() ? textValue : "NodeSpark Synra Core2 is connected.");
  else if (type == "pair") {
    pairCode = doc["code"].as<String>();
    pairWithCode();
  } else {
    bleNotifyEvent("received", type);
  }
}
#else
void setupBle() {}
void processBleCommand() {}
void bleNotifyState() {}
void bleNotifyEvent(const String&, const String&) {}
#endif

void setup() {
  Serial.begin(115200);
  delay(80);
  auto cfg = M5.config();
  cfg.clear_display = true;
  cfg.output_power = true;
  cfg.internal_spk = true;
  cfg.internal_mic = true;
  M5.begin(cfg);
  M5.setTouchButtonHeight(0);
  M5.Display.setRotation(1);
  M5.Display.setBrightness(185);
  splash();
  prefs.begin("synra-core2", false);
  deviceId = makeDeviceId();
  wifiSsid = prefs.getString("ssid", wifiSsid);
  wifiPass = prefs.getString("wifiPass", wifiPass);
  hubBase = trimBase(prefs.getString("hub", hubBase));
  token = prefs.getString("token", "");
  volumeLevel = prefs.getInt("volume", volumeLevel);
  characterIndex = constrain(prefs.getInt("character", characterIndex), 0, SYNRA_CHARACTER_COUNT - 1);
  pairCode = prefs.getString("pendingPair", "");
  prefs.putString("deviceId", deviceId);
  beginSpeaker();
  beginMic();
  checkSd();
  setupBle();
  delay(700);
#if WISP_CONNECT_ON_BOOT
  connectWifi(false);
  if (WiFi.isConnected()) {
    healthCheck();
    if (token.length()) checkIn();
    else if (pairCode.length()) pairWithCode();
  }
#endif
  dirty = true;
}

void loop() {
  M5.update();
  if (portalActive) {
    dns.processNextRequest();
    portal.handleClient();
  }
  processBleCommand();
  handleTouch();
  handleButtons();
  if (WiFi.isConnected() && millis() - lastCheckinMs > WISP_HUB_HEARTBEAT_MS) {
    lastCheckinMs = millis();
    checkIn();
    bleNotifyState();
  }
  if (WiFi.isConnected() && token.length() && millis() - lastPollMs > WISP_COMMAND_POLL_MS) {
    lastPollMs = millis();
    pollCommands();
  }
  updateMicMeter();
  if (dirty) drawScreen();
  delay(15);
}
