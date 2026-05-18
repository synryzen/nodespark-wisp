#include <M5Unified.h>
#include <ArduinoJson.h>
#include <FS.h>
#include <HTTPClient.h>
#include <Preferences.h>
#include <SD.h>
#include <WiFi.h>
#include <WiFiClient.h>
#include <WiFiClientSecure.h>

#if __has_include("config.h")
#include "config.h"
#else
#define WISP_WIFI_SSID ""
#define WISP_WIFI_PASSWORD ""
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
#endif

#include "mascot_logo.h"

static constexpr uint16_t C_BG = 0x0862;
static constexpr uint16_t C_PANEL = 0x10A4;
static constexpr uint16_t C_PANEL2 = 0x18E7;
static constexpr uint16_t C_TEXT = 0xFFFF;
static constexpr uint16_t C_MUTED = 0xBDF7;
static constexpr uint16_t C_CYAN = 0x05FF;
static constexpr uint16_t C_BLUE = 0x351F;
static constexpr uint16_t C_GREEN = 0x4FE8;
static constexpr uint16_t C_AMBER = 0xFEA0;
static constexpr uint16_t C_PINK = 0xF81F;
static constexpr uint16_t C_RED = 0xF9E7;
static constexpr uint16_t C_WHITE = 0xFFFF;
static constexpr int SCREEN_W = 320;
static constexpr int SCREEN_H = 240;
static constexpr int NAV_Y = 206;
static constexpr int NAV_H = 34;

enum Screen : uint8_t {
  SCREEN_STATUS = 0,
  SCREEN_PAIR,
  SCREEN_HUB,
  SCREEN_SENSORS,
  SCREEN_SETUP,
  SCREEN_COUNT
};

struct ButtonRect {
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
String wifiSsid = WISP_WIFI_SSID;
String wifiPassword = WISP_WIFI_PASSWORD;
String hubBase = WISP_HUB_URL;
String deviceName = WISP_DEVICE_NAME;
String defaultWorkflow = WISP_DEFAULT_WORKFLOW;
String deviceId;
String token;
String pairCode;
String lastStatus = "Starting";
String lastCommand = "No Hub command yet";
String pendingApprovalId;
String pendingApprovalText;
bool hubOnline = false;
bool sdReady = false;
bool micReady = false;
bool speakerReady = false;
int volumeLevel = 96;
int micLevel = 0;
float accelX = 0;
float accelY = 0;
float accelZ = 0;
uint32_t lastDrawMs = 0;
uint32_t lastHeartbeatMs = 0;
uint32_t lastCommandPollMs = 0;
uint32_t lastSensorMs = 0;
Screen activeScreen = SCREEN_STATUS;

ButtonRect navButtons[] = {
  {0, NAV_Y, 64, NAV_H, "Status"},
  {64, NAV_Y, 64, NAV_H, "Pair"},
  {128, NAV_Y, 64, NAV_H, "Hub"},
  {192, NAV_Y, 64, NAV_H, "Sense"},
  {256, NAV_Y, 64, NAV_H, "Set"}
};

String jsonEscape(const String& value) {
  String out;
  out.reserve(value.length() + 8);
  for (size_t i = 0; i < value.length(); i++) {
    char c = value[i];
    if (c == '"' || c == '\\') {
      out += '\\';
      out += c;
    } else if (c == '\n') {
      out += "\\n";
    } else if (c == '\r') {
      out += "\\r";
    } else if (c == '\t') {
      out += "\\t";
    } else if ((uint8_t)c < 0x20) {
      out += ' ';
    } else {
      out += c;
    }
  }
  return out;
}

String urlEncode(const String& value) {
  const char* hex = "0123456789ABCDEF";
  String out;
  for (size_t i = 0; i < value.length(); i++) {
    uint8_t c = value[i];
    if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.' || c == '~') {
      out += (char)c;
    } else if (c == ' ') {
      out += "%20";
    } else {
      out += '%';
      out += hex[c >> 4];
      out += hex[c & 0x0F];
    }
  }
  return out;
}

String trimTrailingSlash(String value) {
  value.trim();
  while (value.endsWith("/")) {
    value.remove(value.length() - 1);
  }
  return value;
}

String makeDeviceId() {
  uint64_t mac = ESP.getEfuseMac();
  char buf[37];
  snprintf(buf, sizeof(buf), "00000000-0000-4002-8000-%04X%08X", (uint16_t)(mac >> 32), (uint32_t)mac);
  return String(buf);
}

void savePrefs() {
  prefs.putString("hub", hubBase);
  prefs.putString("token", token);
  prefs.putString("workflow", defaultWorkflow);
  prefs.putInt("volume", volumeLevel);
}

void pulseHaptic(uint16_t ms = 45, uint8_t level = 150) {
#if WISP_ENABLE_HAPTICS
  M5.Power.setVibration(level);
  delay(ms);
  M5.Power.setVibration(0);
#endif
}

void stopMic() {
#if WISP_ENABLE_MIC
  if (micReady) {
    M5.Mic.end();
    micReady = false;
  }
#endif
}

void startMic() {
#if WISP_ENABLE_MIC
  if (!micReady) {
    micReady = M5.Mic.begin();
  }
#endif
}

void playChime(uint16_t accent = C_CYAN) {
#if WISP_ENABLE_SPEAKER
  stopMic();
  if (!speakerReady) {
    speakerReady = M5.Speaker.begin();
  }
  M5.Speaker.setVolume((uint8_t)constrain(volumeLevel, 0, 255));
  M5.Speaker.tone(accent == C_RED ? 260 : 660, 80);
  delay(95);
  M5.Speaker.tone(accent == C_RED ? 190 : 990, 90);
  delay(110);
  startMic();
#endif
}

void drawText(int x, int y, const String& text, uint16_t color = C_TEXT, float size = 1.0f) {
  M5.Display.setTextColor(color, C_BG);
  M5.Display.setTextSize(size);
  M5.Display.setCursor(x, y);
  M5.Display.print(text);
}

void drawWrapped(const String& text, int x, int y, int maxChars, int maxLines, uint16_t color = C_TEXT, float size = 1.0f) {
  String line;
  int lineNo = 0;
  int i = 0;
  while (i < (int)text.length() && lineNo < maxLines) {
    while (i < (int)text.length() && text[i] == ' ') i++;
    String word;
    while (i < (int)text.length() && text[i] != ' ' && text[i] != '\n') {
      word += text[i++];
    }
    if (line.length() + word.length() + 1 > (size_t)maxChars) {
      drawText(x, y + lineNo * (int)(14 * size + 3), line, color, size);
      line = word;
      lineNo++;
    } else {
      if (line.length()) line += ' ';
      line += word;
    }
    if (i < (int)text.length() && text[i] == '\n') {
      drawText(x, y + lineNo * (int)(14 * size + 3), line, color, size);
      line = "";
      lineNo++;
      i++;
    }
  }
  if (line.length() && lineNo < maxLines) {
    drawText(x, y + lineNo * (int)(14 * size + 3), line, color, size);
  }
}

void drawPill(int x, int y, int w, int h, const String& label, uint16_t color, uint16_t fill = C_PANEL2) {
  M5.Display.fillRoundRect(x, y, w, h, 8, fill);
  M5.Display.drawRoundRect(x, y, w, h, 8, color);
  M5.Display.setTextColor(C_TEXT, fill);
  M5.Display.setTextSize(1.0f);
  M5.Display.setCursor(x + 9, y + 8);
  M5.Display.print(label);
}

void drawHeader(const String& title, uint16_t accent = C_CYAN) {
  M5.Display.fillScreen(C_BG);
  M5.Display.fillRect(0, 0, SCREEN_W, 28, 0x0000);
  M5.Display.fillRect(0, 27, SCREEN_W, 2, accent);
  M5.Display.setTextColor(C_TEXT, 0x0000);
  M5.Display.setTextSize(1.0f);
  M5.Display.setCursor(10, 8);
  M5.Display.print(title);
  int batt = M5.Power.getBatteryLevel();
  String state = WiFi.isConnected() ? WiFi.localIP().toString() : "Wi-Fi off";
  M5.Display.setCursor(176, 8);
  M5.Display.print(state.substring(0, 17));
  M5.Display.setCursor(286, 8);
  M5.Display.print(batt >= 0 ? String(batt) + "%" : "--");
}

void drawNav() {
  for (uint8_t i = 0; i < SCREEN_COUNT; i++) {
    bool selected = i == activeScreen;
    uint16_t fill = selected ? C_BLUE : C_PANEL;
    uint16_t stroke = selected ? C_CYAN : 0x3186;
    ButtonRect b = navButtons[i];
    M5.Display.fillRoundRect(b.x + 2, b.y + 3, b.w - 4, b.h - 6, 7, fill);
    M5.Display.drawRoundRect(b.x + 2, b.y + 3, b.w - 4, b.h - 6, 7, stroke);
    M5.Display.setTextColor(C_TEXT, fill);
    M5.Display.setTextSize(0.85f);
    M5.Display.setCursor(b.x + 11, b.y + 13);
    M5.Display.print(b.label);
  }
}

void drawMascotStartup() {
  M5.Display.fillScreen(0x0000);
  for (int y = 0; y < SCREEN_H; y++) {
    uint16_t shade = M5.Display.color565(0, y / 8, 20 + y / 5);
    M5.Display.drawFastHLine(0, y, SCREEN_W, shade);
  }
  M5.Display.fillRoundRect(22, 16, 276, 206, 18, C_BG);
  M5.Display.drawRoundRect(22, 16, 276, 206, 18, C_CYAN);
  M5.Display.drawRoundRect(27, 21, 266, 196, 14, C_BLUE);
  M5.Display.pushImage((SCREEN_W - WISP_MASCOT_W) / 2, 30, WISP_MASCOT_W, WISP_MASCOT_H, WISP_MASCOT);
  M5.Display.setTextColor(C_WHITE, C_BG);
  M5.Display.setTextSize(2.0f);
  M5.Display.setCursor(54, 176);
  M5.Display.print("NodeSpark Wisp");
  M5.Display.setTextSize(1.0f);
  M5.Display.setCursor(86, 203);
  M5.Display.print("Core2 client starting");
}

void showCard(const String& title, const String& body, uint16_t accent = C_CYAN) {
  drawHeader(title, accent);
  M5.Display.fillRoundRect(14, 42, 292, 144, 12, C_PANEL);
  M5.Display.drawRoundRect(14, 42, 292, 144, 12, accent);
  drawWrapped(body, 28, 64, 34, 6, C_TEXT, 1.15f);
  drawNav();
  lastDrawMs = millis();
}

HttpResult httpRequest(const String& method, const String& path, const String& body = "") {
  HttpResult result;
  if (!WiFi.isConnected()) {
    result.error = "Wi-Fi offline";
    return result;
  }
  String base = trimTrailingSlash(hubBase);
  if (!base.startsWith("http://") && !base.startsWith("https://")) {
    base = "https://" + base;
  }
  String url = path.startsWith("http") ? path : base + path;
  HTTPClient http;
  http.setTimeout(WISP_HTTP_TIMEOUT_MS);
  bool ok = false;
  WiFiClient plain;
  WiFiClientSecure secure;
  if (url.startsWith("https://")) {
    secure.setInsecure();
    ok = http.begin(secure, url);
  } else {
    ok = http.begin(plain, url);
  }
  if (!ok) {
    result.error = "HTTP begin failed";
    return result;
  }
  http.addHeader("Content-Type", "application/json");
  http.addHeader("X-NodeSparkHub-Device-ID", deviceId);
  http.addHeader("X-NodeSparkHub-Device-Name", deviceName);
  if (token.length()) {
    http.addHeader("Authorization", "Bearer " + token);
  }
  if (method == "GET") {
    result.status = http.GET();
  } else {
    result.status = http.POST(body);
  }
  result.body = http.getString();
  if (result.status < 200 || result.status >= 300) {
    result.error = "HTTP " + String(result.status);
  }
  http.end();
  return result;
}

bool connectWifi(bool draw = true) {
  if (WiFi.isConnected()) return true;
  if (!wifiSsid.length()) {
    lastStatus = "Set Wi-Fi in config.h";
    return false;
  }
  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);
  WiFi.begin(wifiSsid.c_str(), wifiPassword.c_str());
  uint32_t start = millis();
  while (!WiFi.isConnected() && millis() - start < 12000) {
    M5.update();
    if (draw && millis() - lastDrawMs > 700) {
      showCard("Wi-Fi", "Connecting to " + wifiSsid + " ...", C_BLUE);
    }
    delay(120);
  }
  if (WiFi.isConnected()) {
    lastStatus = "Wi-Fi connected";
    return true;
  }
  lastStatus = "Wi-Fi connect failed";
  return false;
}

void checkSdCard() {
#if WISP_ENABLE_SD
  if (!sdReady) {
    sdReady = SD.begin(4);
  }
  if (sdReady) {
    File file = SD.open("/nodespark-core2.log", FILE_APPEND);
    if (file) {
      file.printf("%lu %s %s\n", millis(), deviceId.c_str(), lastStatus.c_str());
      file.close();
    }
  }
#endif
}

void sampleSensors() {
  if (millis() - lastSensorMs < 350) return;
  lastSensorMs = millis();
  M5.Imu.getAccelData(&accelX, &accelY, &accelZ);
#if WISP_ENABLE_MIC
  if (!micReady) startMic();
  if (micReady) {
    static int16_t buffer[160];
    if (M5.Mic.record(buffer, 160, 16000)) {
      uint32_t total = 0;
      for (size_t i = 0; i < 160; i++) {
        total += abs(buffer[i]);
      }
      micLevel = constrain((int)(total / 160 / 80), 0, 100);
    }
  }
#endif
}

bool healthCheck() {
  HttpResult res = httpRequest("GET", "/health");
  hubOnline = res.status >= 200 && res.status < 300;
  lastStatus = hubOnline ? "Hub online" : ("Hub " + (res.error.length() ? res.error : "offline"));
  return hubOnline;
}

void checkIn() {
  String body = "{";
  body += "\"deviceId\":\"" + jsonEscape(deviceId) + "\",";
  body += "\"name\":\"" + jsonEscape(deviceName) + "\",";
  body += "\"platform\":\"M5Stack Core2 / NodeSpark Wisp\",";
  body += "\"osVersion\":\"Arduino ESP32\",";
  body += "\"ipAddress\":\"" + (WiFi.isConnected() ? WiFi.localIP().toString() : String("")) + "\",";
  body += "\"capabilities\":[\"display\",\"touch\",\"speaker\",\"microphone\",\"imu\",\"haptic\",\"battery\",\"rtc\",\"sd\",\"approval\",\"dashboard\",\"deviceCommands\",\"workflow\",\"assistant\"]";
  body += "}";
  HttpResult res = httpRequest("POST", "/devices/checkin", body);
  hubOnline = res.status >= 200 && res.status < 300;
  lastStatus = hubOnline ? "Checked in with Hub" : ("Check-in " + res.error);
}

void pairWithCode() {
  if (!pairCode.length()) {
    showCard("Pair", "Enter the pairing code from NodeSparkHub Devices.", C_AMBER);
    return;
  }
  String body = "{";
  body += "\"code\":\"" + jsonEscape(pairCode) + "\",";
  body += "\"deviceId\":\"" + jsonEscape(deviceId) + "\",";
  body += "\"deviceName\":\"" + jsonEscape(deviceName) + "\",";
  body += "\"platform\":\"M5Stack Core2 / NodeSpark Wisp\",";
  body += "\"capabilities\":[\"display\",\"touch\",\"speaker\",\"microphone\",\"imu\",\"haptic\",\"battery\",\"rtc\",\"sd\",\"approval\",\"dashboard\",\"deviceCommands\",\"workflow\",\"assistant\"]";
  body += "}";
  showCard("Pairing", "Sending code to NodeSparkHub...", C_BLUE);
  HttpResult res = httpRequest("POST", "/pair", body);
  if (res.status >= 200 && res.status < 300) {
    DynamicJsonDocument doc(2048);
    DeserializationError err = deserializeJson(doc, res.body);
    if (!err && doc["deviceToken"].is<const char*>()) {
      token = doc["deviceToken"].as<String>();
      pairCode = "";
      savePrefs();
      lastStatus = "Paired with NodeSparkHub";
      showCard("Paired", "Core2 is now connected to NodeSparkHub.", C_GREEN);
      playChime(C_GREEN);
      checkIn();
      return;
    }
  }
  lastStatus = "Pair failed " + res.error;
  showCard("Pair Failed", res.error.length() ? res.error + " " + res.body.substring(0, 120) : res.body.substring(0, 160), C_RED);
  playChime(C_RED);
}

void ackCommand(const String& commandId, const String& status, const String& message) {
  if (!commandId.length()) return;
  String body = "{\"status\":\"" + jsonEscape(status) + "\",\"message\":\"" + jsonEscape(message) + "\"}";
  httpRequest("POST", "/devices/" + deviceId + "/commands/" + commandId + "/ack", body);
}

void runWorkflow(const String& input = "M5Stack Core2 Wisp requested a workflow.") {
  if (!token.length()) {
    showCard("Pair Required", "Pair the Core2 before running NodeSparkHub workflows.", C_AMBER);
    return;
  }
  String path = "/workflows/" + urlEncode(defaultWorkflow) + "/run?async=1";
  String body = "{";
  body += "\"input\":\"" + jsonEscape(input) + "\",";
  body += "\"source\":\"nodespark-wisp-core2\",";
  body += "\"deviceId\":\"" + jsonEscape(deviceId) + "\",";
  body += "\"deviceName\":\"" + jsonEscape(deviceName) + "\"";
  body += "}";
  showCard("Workflow", "Running " + defaultWorkflow + " ...", C_BLUE);
  HttpResult res = httpRequest("POST", path, body);
  if (res.status >= 200 && res.status < 300) {
    lastStatus = "Workflow started";
    showCard("Workflow Started", defaultWorkflow, C_GREEN);
    playChime(C_GREEN);
  } else {
    lastStatus = "Workflow failed " + res.error;
    showCard("Workflow Failed", res.error + " " + res.body.substring(0, 130), C_RED);
    playChime(C_RED);
  }
}

void askAssistant(const String& prompt = "Give a short exciting demo of what NodeSpark Wisp Core2 can do.") {
  if (!token.length()) {
    showCard("Pair Required", "Pair the Core2 before using Wisp Assistant.", C_AMBER);
    return;
  }
  String body = "{";
  body += "\"text\":\"" + jsonEscape(prompt) + "\",";
  body += "\"deviceId\":\"" + jsonEscape(deviceId) + "\",";
  body += "\"deviceName\":\"" + jsonEscape(deviceName) + "\",";
  body += "\"platform\":\"M5Stack Core2 / NodeSpark Wisp\",";
  body += "\"sessionId\":\"core2:" + jsonEscape(deviceId) + "\",";
  body += "\"voice\":true,";
  body += "\"capabilities\":[\"display\",\"touch\",\"speaker\",\"microphone\",\"imu\",\"haptic\",\"battery\",\"rtc\",\"sd\",\"assistant\",\"workflow\"]";
  body += "}";
  showCard("Ask AI", "Sending to NodeSparkHub assistant...", C_PINK);
  HttpResult res = httpRequest("POST", "/wisp/assistant", body);
  if (res.status >= 200 && res.status < 300) {
    DynamicJsonDocument doc(4096);
    DeserializationError err = deserializeJson(doc, res.body);
    String reply = !err && doc["displayText"].is<const char*>() ? doc["displayText"].as<String>() : "";
    if (!reply.length()) reply = !err && doc["reply"].is<const char*>() ? doc["reply"].as<String>() : res.body;
    String speechText = !err && doc["speechText"].is<const char*>() ? doc["speechText"].as<String>() : reply;
    lastStatus = "Assistant answered";
    showCard("Wisp Assistant", reply.substring(0, 240), C_PINK);
    if (sdReady && speechText.length()) {
      File file = SD.open("/nodespark-core2.log", FILE_APPEND);
      if (file) {
        file.printf("%lu assistant %s\n", millis(), speechText.substring(0, 96).c_str());
        file.close();
      }
    }
    playChime(C_PINK);
    return;
  }
  showCard("Ask AI", "Direct AI did not answer. Trying the workflow fallback.", C_AMBER);
  runWorkflow("Core2 Wisp asked AI: " + prompt);
}

void handleCommand(JsonVariantConst item) {
  String commandId = item["id"] | item["commandId"] | "";
  String kind = item["type"] | item["kind"] | "display";
  String title = item["title"] | kind;
  String body = item["body"] | item["text"] | item["message"] | "";
  if (!body.length() && item["payload"].is<JsonObjectConst>()) {
    body = item["payload"]["body"] | item["payload"]["text"] | item["payload"]["message"] | "";
    title = item["payload"]["title"] | title;
  }
  kind.toLowerCase();
  lastCommand = kind + ": " + (body.length() ? body : title);

  if (kind == "approval" || kind == "approve") {
    pendingApprovalId = commandId;
    pendingApprovalText = body.length() ? body : "Approve this NodeSparkHub action?";
    activeScreen = SCREEN_HUB;
    showCard("Approval", pendingApprovalText + "\nMiddle approves. Right rejects.", C_AMBER);
    pulseHaptic(120, 180);
    return;
  }
  if (kind == "speak" || kind == "sound" || kind == "chime") {
    showCard(title.length() ? title : "Sound", body.length() ? body : "NodeSparkHub played a Core2 chime.", C_CYAN);
    playChime(C_CYAN);
  } else if (kind == "runworkflow") {
    runWorkflow(body.length() ? body : "Hub command started a workflow from Core2.");
  } else if (kind == "assistant" || kind == "askai") {
    askAssistant(body.length() ? body : "Help me from the M5Stack Core2 Wisp.");
  } else if (kind == "dashboard" || kind == "card" || kind == "display" || kind == "notification" || kind == "notify" || kind == "qr") {
    showCard(title.length() ? title : "NodeSparkHub", body.length() ? body : "Hub updated the Core2 display.", kind == "qr" ? C_GREEN : C_CYAN);
    pulseHaptic(35, 120);
  } else if (kind == "volume") {
    int requested = item["volume"] | item["percent"] | item["payload"]["volume"] | volumeLevel;
    volumeLevel = constrain(requested <= 100 ? map(requested, 0, 100, 0, 255) : requested, 0, 255);
    savePrefs();
    showCard("Volume", "Core2 volume set to " + String(map(volumeLevel, 0, 255, 0, 100)) + "%.", C_GREEN);
    playChime(C_GREEN);
  } else if (kind == "ping" || kind == "health" || kind == "status") {
    showCard("Ping", "NodeSparkHub is talking to this M5Stack Core2 Wisp.", C_GREEN);
    pulseHaptic();
  } else {
    showCard(title.length() ? title : "Hub Command", body.length() ? body : "Command received: " + kind, C_BLUE);
  }
  ackCommand(commandId, "completed", "handled on M5Stack Core2");
}

void pollCommands() {
  if (!token.length() || !WiFi.isConnected()) return;
  HttpResult res = httpRequest("GET", "/devices/" + deviceId + "/commands/poll?limit=4");
  if (res.status < 200 || res.status >= 300) {
    lastStatus = "Poll " + res.error;
    return;
  }
  DynamicJsonDocument doc(8192);
  DeserializationError err = deserializeJson(doc, res.body);
  if (err) {
    lastStatus = "Command JSON error";
    return;
  }
  if (doc.is<JsonArray>()) {
    for (JsonVariantConst item : doc.as<JsonArrayConst>()) handleCommand(item);
  } else if (doc["commands"].is<JsonArray>()) {
    for (JsonVariantConst item : doc["commands"].as<JsonArrayConst>()) handleCommand(item);
  } else if (doc["id"].is<const char*>()) {
    handleCommand(doc.as<JsonObjectConst>());
  }
}

void drawStatusScreen() {
  drawHeader("NodeSpark Wisp Core2", hubOnline ? C_GREEN : C_CYAN);
  M5.Display.fillRoundRect(12, 42, 296, 150, 12, C_PANEL);
  M5.Display.drawRoundRect(12, 42, 296, 150, 12, hubOnline ? C_GREEN : C_BLUE);
  drawText(26, 56, WiFi.isConnected() ? "Wi-Fi connected" : "Wi-Fi offline", WiFi.isConnected() ? C_GREEN : C_AMBER, 1.1f);
  drawText(26, 76, "Hub: " + String(hubOnline ? "online" : "not checked"), hubOnline ? C_GREEN : C_MUTED, 1.1f);
  drawText(26, 96, token.length() ? "Paired with NodeSparkHub" : "Not paired", token.length() ? C_GREEN : C_AMBER, 1.1f);
  drawText(26, 116, "Workflow: " + defaultWorkflow.substring(0, 28), C_TEXT, 1.0f);
  drawText(26, 136, "Device: " + deviceId.substring(deviceId.length() > 10 ? deviceId.length() - 10 : 0), C_MUTED, 1.0f);
  drawWrapped(lastStatus, 26, 158, 36, 2, C_MUTED, 0.9f);
  drawNav();
}

void drawPairScreen() {
  drawHeader("Pair With NodeSparkHub", C_AMBER);
  drawText(18, 40, "Code from Hub Server -> Devices", C_MUTED, 0.95f);
  M5.Display.fillRoundRect(18, 58, 284, 30, 8, C_PANEL);
  drawText(34, 66, pairCode.length() ? pairCode : "Enter pairing code", pairCode.length() ? C_TEXT : C_MUTED, 1.2f);
  int x0 = 22;
  int y0 = 98;
  for (int i = 0; i < 10; i++) {
    int col = i % 5;
    int row = i / 5;
    drawPill(x0 + col * 46, y0 + row * 34, 38, 27, String(i), C_BLUE, C_PANEL2);
  }
  drawPill(254, 98, 48, 27, "Del", C_AMBER, C_PANEL2);
  drawPill(254, 132, 48, 27, "Pair", C_GREEN, C_PANEL2);
  drawWrapped("The Core2 saves its token after pairing.", 18, 170, 38, 2, C_MUTED, 0.9f);
  drawNav();
}

void drawHubScreen() {
  drawHeader("Hub Actions", C_PINK);
  drawPill(18, 42, 132, 34, "Ask AI", C_PINK);
  drawPill(170, 42, 132, 34, "Run Workflow", C_GREEN);
  drawPill(18, 86, 132, 34, "Ping Hub", C_BLUE);
  drawPill(170, 86, 132, 34, pendingApprovalId.length() ? "Approve" : "Chime", pendingApprovalId.length() ? C_AMBER : C_CYAN);
  if (pendingApprovalId.length()) {
    drawWrapped("Pending: " + pendingApprovalText, 18, 134, 40, 3, C_AMBER, 0.95f);
    drawText(18, 184, "A: reject  B: approve", C_MUTED, 0.9f);
  } else {
    drawWrapped("Last command: " + lastCommand, 18, 136, 40, 3, C_MUTED, 0.95f);
  }
  drawNav();
}

void drawSensorsScreen() {
  drawHeader("Sensors + Hardware", C_GREEN);
  int batt = M5.Power.getBatteryLevel();
  bool charging = M5.Power.isCharging() == m5::Power_Class::is_charging;
  drawText(18, 42, "Battery: " + String(batt >= 0 ? String(batt) + "%" : "--") + (charging ? " charging" : ""), C_TEXT, 1.0f);
  drawText(18, 62, "SD card: " + String(sdReady ? "ready" : "not ready"), sdReady ? C_GREEN : C_AMBER, 1.0f);
  drawText(18, 82, "Mic: " + String(micReady ? "ready" : "off") + "   Speaker: " + String(speakerReady ? "ready" : "ready on play"), C_MUTED, 0.95f);
  M5.Display.drawRoundRect(18, 106, 284, 18, 5, C_CYAN);
  M5.Display.fillRoundRect(20, 108, map(micLevel, 0, 100, 0, 280), 14, 4, C_CYAN);
  drawText(20, 130, "Mic level " + String(micLevel) + "%", C_MUTED, 0.9f);
  drawText(18, 152, "IMU x " + String(accelX, 2) + " y " + String(accelY, 2) + " z " + String(accelZ, 2), C_TEXT, 0.95f);
  drawPill(18, 174, 132, 26, "Vibrate", C_GREEN);
  drawPill(170, 174, 132, 26, "SD Check", sdReady ? C_GREEN : C_AMBER);
  drawNav();
}

void drawSetupScreen() {
  drawHeader("Core2 Setup", C_BLUE);
  drawPill(18, 42, 132, 34, "Vol -", C_CYAN);
  drawPill(170, 42, 132, 34, "Vol +", C_CYAN);
  drawText(22, 84, "Volume: " + String(map(volumeLevel, 0, 255, 0, 100)) + "%", C_TEXT, 1.0f);
  drawPill(18, 108, 132, 34, "Reconnect", C_GREEN);
  drawPill(170, 108, 132, 34, "Forget Pair", C_RED);
  drawWrapped("Hub: " + hubBase, 18, 154, 39, 2, C_MUTED, 0.9f);
  drawWrapped("Wi-Fi and Hub URL are set in config.h for this build.", 18, 182, 40, 2, C_MUTED, 0.85f);
  drawNav();
}

void drawActiveScreen(bool force = false) {
  if (!force && millis() - lastDrawMs < 250) return;
  lastDrawMs = millis();
  switch (activeScreen) {
    case SCREEN_STATUS: drawStatusScreen(); break;
    case SCREEN_PAIR: drawPairScreen(); break;
    case SCREEN_HUB: drawHubScreen(); break;
    case SCREEN_SENSORS: drawSensorsScreen(); break;
    case SCREEN_SETUP: drawSetupScreen(); break;
    default: drawStatusScreen(); break;
  }
}

bool inside(int x, int y, const ButtonRect& b) {
  return x >= b.x && x < b.x + b.w && y >= b.y && y < b.y + b.h;
}

void handlePairTouch(int x, int y) {
  if (y >= 98 && y < 159) {
    for (int i = 0; i < 10; i++) {
      int bx = 22 + (i % 5) * 46;
      int by = 98 + (i / 5) * 34;
      if (x >= bx && x <= bx + 38 && y >= by && y <= by + 27) {
        if (pairCode.length() < 12) pairCode += String(i);
        pulseHaptic(20, 90);
        drawPairScreen();
        return;
      }
    }
    if (x >= 254 && x <= 302 && y >= 98 && y <= 125) {
      if (pairCode.length()) pairCode.remove(pairCode.length() - 1);
      drawPairScreen();
      return;
    }
    if (x >= 254 && x <= 302 && y >= 132 && y <= 159) {
      pairWithCode();
      return;
    }
  }
}

void handleHubTouch(int x, int y) {
  if (y >= 42 && y <= 76) {
    if (x < 160) askAssistant();
    else runWorkflow();
  } else if (y >= 86 && y <= 120) {
    if (x < 160) {
      healthCheck();
      showCard("Hub Ping", lastStatus, hubOnline ? C_GREEN : C_RED);
    } else if (pendingApprovalId.length()) {
      ackCommand(pendingApprovalId, "approved", "approved on M5Stack Core2");
      pendingApprovalId = "";
      pendingApprovalText = "";
      showCard("Approved", "NodeSparkHub approval sent.", C_GREEN);
      playChime(C_GREEN);
    } else {
      showCard("Sound", "Playing Core2 chime.", C_CYAN);
      playChime(C_CYAN);
    }
  }
}

void handleSensorsTouch(int x, int y) {
  if (y >= 174 && y <= 200) {
    if (x < 160) {
      pulseHaptic(220, 180);
    } else {
      sdReady = false;
      checkSdCard();
      showCard("SD Card", sdReady ? "microSD is mounted and log write was attempted." : "microSD did not mount. Use FAT32 and insert before boot.", sdReady ? C_GREEN : C_AMBER);
    }
  }
}

void handleSetupTouch(int x, int y) {
  if (y >= 42 && y <= 76) {
    if (x < 160) volumeLevel = max(0, volumeLevel - 24);
    else volumeLevel = min(255, volumeLevel + 24);
    savePrefs();
    drawSetupScreen();
    playChime(C_CYAN);
  } else if (y >= 108 && y <= 142) {
    if (x < 160) {
      WiFi.disconnect();
      connectWifi(true);
      healthCheck();
      drawStatusScreen();
    } else {
      token = "";
      prefs.remove("token");
      pendingApprovalId = "";
      showCard("Pairing Cleared", "Open Pair and enter a fresh NodeSparkHub pairing code.", C_AMBER);
    }
  }
}

void handleTouch() {
  auto td = M5.Touch.getDetail();
  if (!td.wasClicked()) return;
  int x = td.x;
  int y = td.y;
  for (uint8_t i = 0; i < SCREEN_COUNT; i++) {
    if (inside(x, y, navButtons[i])) {
      activeScreen = (Screen)i;
      pulseHaptic(20, 80);
      drawActiveScreen(true);
      return;
    }
  }
  if (activeScreen == SCREEN_PAIR) handlePairTouch(x, y);
  else if (activeScreen == SCREEN_HUB) handleHubTouch(x, y);
  else if (activeScreen == SCREEN_SENSORS) handleSensorsTouch(x, y);
  else if (activeScreen == SCREEN_SETUP) handleSetupTouch(x, y);
}

void handleButtons() {
  if (M5.BtnA.wasClicked()) {
    if (pendingApprovalId.length() && activeScreen == SCREEN_HUB) {
      ackCommand(pendingApprovalId, "rejected", "rejected on M5Stack Core2");
      pendingApprovalId = "";
      pendingApprovalText = "";
      showCard("Rejected", "NodeSparkHub approval rejected.", C_RED);
      playChime(C_RED);
      return;
    }
    activeScreen = (Screen)((activeScreen + SCREEN_COUNT - 1) % SCREEN_COUNT);
    drawActiveScreen(true);
  }
  if (M5.BtnC.wasClicked()) {
    activeScreen = (Screen)((activeScreen + 1) % SCREEN_COUNT);
    drawActiveScreen(true);
  }
  if (M5.BtnB.wasClicked()) {
    if (activeScreen == SCREEN_PAIR) pairWithCode();
    else if (activeScreen == SCREEN_HUB) {
      if (pendingApprovalId.length()) {
        ackCommand(pendingApprovalId, "approved", "approved on M5Stack Core2");
        pendingApprovalId = "";
        pendingApprovalText = "";
        showCard("Approved", "NodeSparkHub approval sent.", C_GREEN);
        playChime(C_GREEN);
      } else {
        askAssistant();
      }
    } else if (activeScreen == SCREEN_SENSORS) {
      playChime(C_CYAN);
      pulseHaptic(90, 160);
    } else if (activeScreen == SCREEN_SETUP) {
      connectWifi(true);
      healthCheck();
      drawStatusScreen();
      activeScreen = SCREEN_STATUS;
    } else {
      runWorkflow();
    }
  }
}

void setup() {
  Serial.begin(115200);
  auto cfg = M5.config();
  cfg.clear_display = true;
  cfg.output_power = true;
  cfg.internal_spk = true;
  cfg.internal_mic = true;
  M5.begin(cfg);
  M5.setTouchButtonHeight(34);
  M5.Display.setRotation(1);
  M5.Display.setTextSize(1);
  drawMascotStartup();
  pulseHaptic(60, 140);

  prefs.begin("wisp-core2", false);
  hubBase = trimTrailingSlash(prefs.getString("hub", WISP_HUB_URL));
  token = prefs.getString("token", "");
  defaultWorkflow = prefs.getString("workflow", WISP_DEFAULT_WORKFLOW);
  volumeLevel = prefs.getInt("volume", 96);
  deviceId = makeDeviceId();
  prefs.putString("deviceId", deviceId);

  startMic();
  checkSdCard();
  delay(900);

#if WISP_CONNECT_ON_BOOT
  connectWifi(true);
  if (WiFi.isConnected()) {
    healthCheck();
    if (token.length()) checkIn();
  }
#endif
  drawActiveScreen(true);
  playChime(C_CYAN);
}

void loop() {
  M5.update();
  handleTouch();
  handleButtons();
  sampleSensors();

  if (WiFi.isConnected() && millis() - lastHeartbeatMs > WISP_HUB_HEARTBEAT_MS) {
    lastHeartbeatMs = millis();
    if (token.length()) checkIn();
    else healthCheck();
  }
  if (WiFi.isConnected() && token.length() && millis() - lastCommandPollMs > WISP_COMMAND_POLL_MS) {
    lastCommandPollMs = millis();
    pollCommands();
  }
  if (activeScreen == SCREEN_SENSORS || activeScreen == SCREEN_STATUS) {
    drawActiveScreen(false);
  }
  delay(15);
}
