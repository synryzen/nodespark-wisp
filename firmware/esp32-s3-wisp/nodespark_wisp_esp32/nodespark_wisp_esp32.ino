#include <Arduino.h>
#include <ArduinoJson.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ILI9341.h>
#include <HTTPClient.h>
#include <Preferences.h>
#include <SPI.h>
#include <WiFi.h>
#include <XPT2046_Touchscreen.h>
#include "driver/i2s.h"
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

// ESP32-S3 DevKit pin plan. Change here if your board labels differ.
static constexpr int PIN_TFT_CS = 10;
static constexpr int PIN_TFT_DC = 9;
static constexpr int PIN_TFT_RST = -1;
static constexpr int PIN_SPI_MOSI = 11;
static constexpr int PIN_SPI_MISO = 13;
static constexpr int PIN_SPI_SCK = 12;
static constexpr int PIN_TOUCH_CS = 7;
static constexpr int PIN_TOUCH_IRQ = 6;

static constexpr int PIN_AMP_BCLK = 4;
static constexpr int PIN_AMP_LRCLK = 5;
static constexpr int PIN_AMP_DIN = 16;

static constexpr int PIN_MIC_SCK = 15;
static constexpr int PIN_MIC_WS = 17;
static constexpr int PIN_MIC_SD = 18;

static constexpr int SCREEN_W = 320;
static constexpr int SCREEN_H = 240;
static constexpr const char* APP_VERSION = "nodespark-wisp-esp32/0.1.0";

static constexpr int TL_DATUM = 0;
static constexpr int ML_DATUM = 1;
static constexpr int MR_DATUM = 2;
static constexpr int MC_DATUM = 3;

class WispTft {
public:
  Adafruit_ILI9341 display;
  int datum = TL_DATUM;
  uint16_t textColor = ILI9341_WHITE;

  WispTft() : display(PIN_TFT_CS, PIN_TFT_DC, PIN_TFT_RST) {}

  void init() {
    pinMode(PIN_TFT_CS, OUTPUT);
    pinMode(PIN_TOUCH_CS, OUTPUT);
    digitalWrite(PIN_TFT_CS, HIGH);
    digitalWrite(PIN_TOUCH_CS, HIGH);
    SPI.begin(PIN_SPI_SCK, PIN_SPI_MISO, PIN_SPI_MOSI, PIN_TFT_CS);
    display.begin(8000000);
    digitalWrite(PIN_TFT_CS, HIGH);
  }

  void guardDisplay() { digitalWrite(PIN_TOUCH_CS, HIGH); }
  void setRotation(uint8_t rotation) { guardDisplay(); display.setRotation(rotation); }
  void fillScreen(uint16_t color) { guardDisplay(); display.fillScreen(color); }
  void fillRect(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t color) { guardDisplay(); display.fillRect(x, y, w, h, color); }
  void drawRect(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t color) { guardDisplay(); display.drawRect(x, y, w, h, color); }
  void drawFastVLine(int16_t x, int16_t y, int16_t h, uint16_t color) { guardDisplay(); display.drawFastVLine(x, y, h, color); }
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

enum Screen { SCREEN_STATUS, SCREEN_PAIR, SCREEN_COMMANDS, SCREEN_DEMO, SCREEN_MIC, SCREEN_SETUP };
Screen currentScreen = SCREEN_STATUS;

enum SetupView { SETUP_MAIN, SETUP_WIFI_LIST, SETUP_INPUT };
enum InputTarget { INPUT_NONE, INPUT_SSID, INPUT_WIFI_PASSWORD, INPUT_HUB_BASE, INPUT_HUB_PORT };
SetupView setupView = SETUP_MAIN;
InputTarget inputTarget = INPUT_NONE;

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

uint32_t lastCheckinMs = 0;
uint32_t lastPollMs = 0;
uint32_t lastWifiDrawMs = 0;
bool ampReady = false;
bool micReady = false;

struct Button {
  int16_t x;
  int16_t y;
  int16_t w;
  int16_t h;
  String label;
  uint16_t color;
};

static const uint16_t C_BG = ILI9341_BLACK;
static const uint16_t C_PANEL = 0x1084;
static const uint16_t C_BLUE = 0x05FF;
static const uint16_t C_PINK = 0xF81F;
static const uint16_t C_GREEN = 0x07E0;
static const uint16_t C_AMBER = 0xFD20;
static const uint16_t C_RED = 0xF9A6;
static const uint16_t C_MUTED = 0x9CF3;

String macId() {
  uint64_t mac = ESP.getEfuseMac();
  char buf[24];
  snprintf(buf, sizeof(buf), "ESP32S3-%04X%08X", (uint16_t)(mac >> 32), (uint32_t)mac);
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

String repeatedChar(char c, int count) {
  String out;
  for (int i = 0; i < count; i++) out += c;
  return out;
}

String normalizedHubBase(String input) {
  input.trim();
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
  hubUrl = hubBase;
  if (hubPort.length()) hubUrl += ":" + hubPort;
}

void loadNetworkSettings() {
  wifiSsid = prefs.getString("wifiSsid", WISP_WIFI_SSID);
  wifiPassword = prefs.getString("wifiPass", WISP_WIFI_PASSWORD);
  hubBase = prefs.getString("hubBase", normalizedHubBase(WISP_HUB_URL));
  hubPort = prefs.getString("hubPort", portFromHubUrl(WISP_HUB_URL, "8787"));
  updateHubUrl();
}

void saveNetworkSettings() {
  updateHubUrl();
  prefs.putString("wifiSsid", wifiSsid);
  prefs.putString("wifiPass", wifiPassword);
  prefs.putString("hubBase", hubBase);
  prefs.putString("hubPort", hubPort);
  lastStatus = "Settings saved.";
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
  tft.setTextColor(ILI9341_BLACK, b.color);
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
  tft.setTextColor(ILI9341_WHITE, C_BG);
  tft.drawString("Device", 14, 56, 2);
  tft.drawString(deviceId, 86, 56, 2);
  tft.drawString("Hub", 14, 78, 2);
  drawWrapped(hubUrl, 86, 78, 26, 2, C_MUTED);
  tft.drawString("Pairing", 14, 120, 2);
  tft.setTextColor(token.length() ? C_GREEN : C_AMBER, C_BG);
  tft.drawString(token.length() ? "Paired" : "Needs code", 86, 120, 2);
  tft.setTextColor(C_MUTED, C_BG);
  drawWrapped(lastStatus, 14, 148, 34, 2, C_MUTED);
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
  drawButton({170, 56, 136, 42, "Local Card", C_BLUE});
  drawButton({14, 112, 136, 42, "Run Workflow", C_AMBER});
  drawButton({170, 112, 136, 42, "Chime", C_PINK});
  drawWrapped("Use this screen at a booth: touch actions prove Wisp is a real NodeSparkHub surface.", 14, 166, 38, 2, C_MUTED);
  drawTabs();
}

void drawMic() {
  drawHeader("INMP441 Mic", C_PINK);
  tft.setTextColor(ILI9341_WHITE, C_BG);
  tft.drawString("Live level", 16, 58, 4);
  drawButton({20, 150, 128, 38, "Sample", C_BLUE});
  drawButton({172, 150, 128, 38, "Voice Run", C_AMBER});
  drawWrapped("Voice Run sends a workflow event. Raw audio upload is reserved for a later Hub endpoint.", 16, 102, 36, 2, C_MUTED);
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
  drawHeader("Wisp Setup", C_AMBER);
  drawButton({12, 50, 92, 30, "Scan", C_BLUE});
  drawButton({114, 50, 92, 30, "Connect", C_GREEN});
  drawButton({216, 50, 92, 30, "Save", C_PINK});

  tft.setTextColor(C_MUTED, C_BG);
  tft.drawString("SSID", 14, 90, 2);
  tft.fillRoundRect(74, 86, 232, 24, 6, C_PANEL);
  tft.setTextColor(ILI9341_WHITE, C_PANEL);
  tft.drawString(wifiSsid.length() ? wifiSsid : "tap or scan", 82, 92, 2);

  tft.setTextColor(C_MUTED, C_BG);
  tft.drawString("Pass", 14, 120, 2);
  tft.fillRoundRect(74, 116, 232, 24, 6, C_PANEL);
  tft.setTextColor(ILI9341_WHITE, C_PANEL);
  tft.drawString(wifiPassword.length() ? repeatedChar('*', min(14, (int)wifiPassword.length())) : "tap to enter", 82, 122, 2);

  tft.setTextColor(C_MUTED, C_BG);
  tft.drawString("URL", 14, 150, 2);
  tft.fillRoundRect(74, 146, 232, 24, 6, C_PANEL);
  tft.setTextColor(ILI9341_WHITE, C_PANEL);
  drawWrapped(hubBase, 82, 152, 27, 1, ILI9341_WHITE);

  tft.setTextColor(C_MUTED, C_BG);
  tft.drawString("Port", 14, 180, 2);
  tft.fillRoundRect(74, 176, 90, 22, 6, C_PANEL);
  tft.setTextColor(ILI9341_WHITE, C_PANEL);
  tft.drawString(hubPort.length() ? hubPort : "none", 82, 182, 2);
  tft.setTextColor(WiFi.isConnected() ? C_GREEN : C_AMBER, C_BG);
  drawWrapped(lastStatus, 174, 176, 16, 1, WiFi.isConnected() ? C_GREEN : C_AMBER);
  drawTabs();
}

void drawWifiList() {
  drawHeader("Choose Wi-Fi", C_BLUE);
  drawButton({12, 50, 90, 28, "Rescan", C_BLUE});
  drawButton({218, 50, 90, 28, "Back", C_AMBER});
  if (!scannedCount) {
    drawWrapped("No networks found. Tap Rescan or enter SSID manually from Setup.", 14, 96, 36, 3, C_MUTED);
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
  if (inputTarget == INPUT_WIFI_PASSWORD && shown.length()) shown = repeatedChar('*', min(22, (int)shown.length()));
  if (!shown.length()) shown = inputTarget == INPUT_HUB_BASE ? "https://..." : "";
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
  delayMicroseconds(8);
  if (!touch.touched()) {
    digitalWrite(PIN_TOUCH_CS, HIGH);
    digitalWrite(PIN_TFT_CS, HIGH);
    return false;
  }
  TS_Point p = touch.getPoint();
  digitalWrite(PIN_TOUCH_CS, HIGH);
  digitalWrite(PIN_TFT_CS, HIGH);
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

bool connectWifi(bool splash) {
  wifiSsid.trim();
  if (!wifiSsid.length()) {
    lastStatus = "Choose Wi-Fi in Setup.";
    if (!splash) drawSetup();
    return false;
  }
  Serial.printf("[wifi] connecting to %s\n", wifiSsid.c_str());
  WiFi.mode(WIFI_STA);
  WiFi.disconnect(false);
  delay(120);
  WiFi.begin(wifiSsid.c_str(), wifiPassword.c_str());
  lastStatus = "Connecting Wi-Fi...";
  if (splash) drawSplash(lastStatus);
  else drawSettingsMain();
  uint32_t start = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - start < 15000) delay(250);
  lastStatus = WiFi.isConnected() ? "Wi-Fi " + WiFi.localIP().toString() : "Wi-Fi failed.";
  Serial.printf("[wifi] %s\n", lastStatus.c_str());
  return WiFi.isConnected();
}

void scanWifiNetworks() {
  setupView = SETUP_WIFI_LIST;
  drawHeader("Choose Wi-Fi", C_BLUE);
  drawWrapped("Scanning nearby networks...", 16, 72, 34, 2, C_MUTED);
  WiFi.mode(WIFI_STA);
  int found = WiFi.scanNetworks(false, true);
  scannedCount = constrain(found, 0, 6);
  for (int i = 0; i < scannedCount; i++) {
    scannedSsids[i] = WiFi.SSID(i);
    scannedRssi[i] = WiFi.RSSI(i);
  }
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

void handleSetupTouch(int x, int y) {
  if (setupView == SETUP_INPUT) {
    handleKeyboardTouch(x, y);
    return;
  }
  if (setupView == SETUP_WIFI_LIST) {
    if (inBox(x, y, {12, 50, 90, 28, "", C_BLUE})) {
      scanWifiNetworks();
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

  if (inBox(x, y, {12, 50, 92, 30, "", C_BLUE})) scanWifiNetworks();
  else if (inBox(x, y, {114, 50, 92, 30, "", C_GREEN})) {
    saveNetworkSettings();
    connectWifi(false);
  }
  else if (inBox(x, y, {216, 50, 92, 30, "", C_PINK})) {
    saveNetworkSettings();
    drawSettingsMain();
  } else if (inBox(x, y, {74, 86, 232, 24, "", C_PANEL})) beginInput(INPUT_SSID, wifiSsid);
  else if (inBox(x, y, {74, 116, 232, 24, "", C_PANEL})) beginInput(INPUT_WIFI_PASSWORD, wifiPassword);
  else if (inBox(x, y, {74, 146, 232, 24, "", C_PANEL})) beginInput(INPUT_HUB_BASE, hubBase);
  else if (inBox(x, y, {74, 176, 90, 22, "", C_PANEL})) beginInput(INPUT_HUB_PORT, hubPort);
}

String request(const String& method, const String& path, const String& body = "", bool auth = true) {
  if (!WiFi.isConnected()) return "";
  HTTPClient http;
  http.begin(hubUrl + path);
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
  if (code < 200 || code >= 300) {
    lastStatus = "HTTP " + String(code) + " " + payload.substring(0, 70);
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
  if (deserializeJson(doc, payload)) return false;
  token = doc["deviceToken"].as<String>();
  if (!token.length()) return false;
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
  if (payload.length()) lastStatus = "Hub online. Commands ready.";
}

void showCard(const String& title, const String& body, uint16_t accent = C_BLUE) {
  drawHeader(title.length() ? title : "NodeSparkHub", accent);
  tft.fillRoundRect(16, 58, SCREEN_W - 32, 118, 12, C_PANEL);
  tft.drawRoundRect(16, 58, SCREEN_W - 32, 118, 12, accent);
  drawWrapped(body, 28, 78, 34, 5, ILI9341_WHITE);
  drawTabs();
}

void drawDashboard(const String& title, const String& label, const String& value) {
  drawHeader(title.length() ? title : "Workflow Monitor", C_GREEN);
  tft.fillRoundRect(18, 58, 284, 66, 12, C_PANEL);
  tft.setTextColor(C_MUTED, C_PANEL);
  tft.drawString(label.length() ? label : "Status", 32, 70, 2);
  tft.setTextColor(C_GREEN, C_PANEL);
  tft.drawString(value.length() ? value : "Live", 32, 88, 4);
  drawWrapped("Server online   Touch ready   Device paired", 22, 142, 36, 2, C_MUTED);
  drawTabs();
}

void playChime(int kind = 0) {
  if (!ampReady) return;
  const int sampleRate = 22050;
  int freqs[3] = {660, kind == 1 ? 330 : 880, kind == 2 ? 220 : 1320};
  for (int f : freqs) {
    for (int i = 0; i < sampleRate / 10; i++) {
      float phase = 2.0f * PI * f * i / sampleRate;
      int16_t sample = (int16_t)(sin(phase) * 9000);
      int16_t stereo[2] = {sample, sample};
      size_t written = 0;
      i2s_write(I2S_NUM_0, stereo, sizeof(stereo), &written, portMAX_DELAY);
    }
  }
}

void executeCommand(JsonObject command) {
  String id = command["id"].as<String>();
  String kind = command["type"].as<String>();
  kind.toLowerCase();
  if (!kind.length()) kind = "display";

  if (kind == "display" || kind == "message" || kind == "show") {
    String title = command["title"] | "NodeSparkHub";
    String body = command["body"] | command["text"] | "";
    showCard(title, body, C_BLUE);
    lastCommand = title + ": " + body;
    playChime(0);
    ackCommand(id, "completed", "displayed");
  } else if (kind == "card" || kind == "demo" || kind == "showcase" || kind == "alert" || kind == "ai") {
    String title = command["title"] | "NodeSparkHub Card";
    String body = command["body"] | command["text"] | "";
    showCard(title, body, C_PINK);
    lastCommand = "Card: " + title;
    playChime(0);
    ackCommand(id, "completed", "card shown");
  } else if (kind == "dashboard" || kind == "metrics") {
    drawDashboard(command["title"] | "Workflow Monitor", command["metricLabel"] | "Hub", command["metricValue"] | "Live");
    lastCommand = "Dashboard shown";
    playChime(0);
    ackCommand(id, "completed", "dashboard shown");
  } else if (kind == "approval" || kind == "approve" || kind == "decision") {
    pendingApprovalId = id;
    pendingApprovalTitle = command["title"] | "Approval Needed";
    pendingApprovalBody = command["body"] | command["text"] | "Review this request.";
    currentScreen = SCREEN_COMMANDS;
    drawCommands();
    playChime(2);
  } else if (kind == "speak" || kind == "speaker" || kind == "tts") {
    String text = command["text"] | command["body"] | "";
    showCard("Speaking", text, C_PINK);
    playChime(0);
    ackCommand(id, "completed", "chime played; TTS pending");
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

void pollCommands() {
  if (!token.length()) return;
  String payload = request("GET", "/devices/" + deviceId + "/commands/poll?limit=4");
  if (!payload.length()) return;
  StaticJsonDocument<3072> doc;
  if (deserializeJson(doc, payload)) return;
  JsonArray commands = doc["commands"].as<JsonArray>();
  for (JsonObject command : commands) executeCommand(command);
}

void runWorkflow(const String& text) {
  if (!token.length()) {
    lastStatus = "Pair device before running workflows.";
    redraw();
    return;
  }
  String path = "/workflows/" + defaultWorkflow + "/run";
  path.replace(" ", "%20");
  String body = "{";
  body += "\"source\":\"wisp-esp32\",";
  body += "\"deviceId\":\"" + jsonEscape(deviceId) + "\",";
  body += "\"deviceName\":\"" + jsonEscape(deviceName) + "\",";
  body += "\"text\":\"" + jsonEscape(text) + "\",";
  body += "\"input\":\"" + jsonEscape(text) + "\"}";
  String payload = request("POST", path, body);
  lastStatus = payload.length() ? "Workflow sent to Hub." : "Workflow failed.";
  redraw();
}

int sampleMicLevel() {
  if (!micReady) return 0;
  int32_t samples[256];
  size_t bytesRead = 0;
  i2s_read(I2S_NUM_1, samples, sizeof(samples), &bytesRead, pdMS_TO_TICKS(120));
  int count = bytesRead / sizeof(int32_t);
  if (count <= 0) return 0;
  uint64_t sum = 0;
  for (int i = 0; i < count; i++) sum += abs(samples[i] >> 14);
  return constrain((int)(sum / count), 0, 1023);
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
      showCard("NodeSparkHub", "ESP32-S3 Wisp touch display is alive.", C_BLUE);
      playChime(0);
    } else if (inBox(x, y, {14, 112, 136, 42, "", C_AMBER})) {
      runWorkflow("ESP32-S3 Wisp touchscreen requested a workflow.");
    } else if (inBox(x, y, {170, 112, 136, 42, "", C_PINK})) {
      playChime(2);
    }
  } else if (currentScreen == SCREEN_MIC) {
    if (inBox(x, y, {20, 150, 128, 38, "", C_BLUE})) {
      int level = sampleMicLevel();
      drawMic();
      tft.fillRoundRect(18, 92, map(level, 0, 1023, 6, 284), 22, 8, C_GREEN);
    } else if (inBox(x, y, {172, 150, 128, 38, "", C_AMBER})) {
      runWorkflow("ESP32-S3 Wisp voice button pressed. Audio upload support will be added next.");
    }
  }
}

void setupAudio() {
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
  i2s_pin_config_t ampPins = {PIN_AMP_BCLK, PIN_AMP_LRCLK, PIN_AMP_DIN, I2S_PIN_NO_CHANGE};
  ampReady = i2s_driver_install(I2S_NUM_0, &ampConfig, 0, nullptr) == ESP_OK &&
             i2s_set_pin(I2S_NUM_0, &ampPins) == ESP_OK;

  i2s_config_t micConfig = {};
  micConfig.mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_RX);
  micConfig.sample_rate = 16000;
  micConfig.bits_per_sample = I2S_BITS_PER_SAMPLE_32BIT;
  micConfig.channel_format = I2S_CHANNEL_FMT_ONLY_LEFT;
  micConfig.communication_format = I2S_COMM_FORMAT_STAND_I2S;
  micConfig.intr_alloc_flags = ESP_INTR_FLAG_LEVEL1;
  micConfig.dma_buf_count = 4;
  micConfig.dma_buf_len = 256;
  micConfig.use_apll = false;
  i2s_pin_config_t micPins = {PIN_MIC_SCK, PIN_MIC_WS, I2S_PIN_NO_CHANGE, PIN_MIC_SD};
  micReady = i2s_driver_install(I2S_NUM_1, &micConfig, 0, nullptr) == ESP_OK &&
             i2s_set_pin(I2S_NUM_1, &micPins) == ESP_OK;
}

void setupWifi() {
  connectWifi(true);
}

void setup() {
  Serial.begin(115200);
  delay(200);
  Serial.println();
  Serial.println("[boot] NodeSpark Wisp ESP32-S3 starting");
  deviceId = macId();
  Serial.printf("[boot] deviceId=%s\n", deviceId.c_str());
  prefs.begin("wisp", false);
  token = prefs.getString("token", "");
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
  setupWifi();
  redraw();
  playChime(0);
  Serial.println("[boot] ready");
}

void loop() {
  int x, y;
  if (touched(x, y)) {
    handleTouch(x, y);
    delay(220);
  }

  if (WiFi.isConnected() && token.length() && millis() - lastCheckinMs > 60000) {
    lastCheckinMs = millis();
    checkin();
    if (currentScreen == SCREEN_STATUS) redraw();
  }
  if (WiFi.isConnected() && token.length() && millis() - lastPollMs > 2000) {
    lastPollMs = millis();
    pollCommands();
  }
  if (millis() - lastWifiDrawMs > 10000 && currentScreen == SCREEN_STATUS) {
    lastWifiDrawMs = millis();
    drawStatus();
  }
}
