#include <Adafruit_GFX.h>
#include <Adafruit_ILI9341.h>
#include <SPI.h>

// Independent ILI9341 diagnostic. This does not use the NodeSpark Wisp firmware
// or TFT_eSPI, so it is useful for proving display wiring.

#define TFT_CS   10
#define TFT_DC   9
#define TFT_RST  -1
#define TFT_MOSI 11
#define TFT_MISO 13
#define TFT_SCK  12

Adafruit_ILI9341 tft(TFT_CS, TFT_DC, TFT_RST);

void label(const char* text, uint16_t color) {
  tft.fillScreen(color);
  tft.setTextColor(ILI9341_WHITE);
  tft.setTextSize(3);
  tft.setCursor(18, 80);
  tft.print(text);
  tft.setTextSize(2);
  tft.setCursor(18, 130);
  tft.print("NodeSpark Wisp");
}

void setup() {
  Serial.begin(115200);
  delay(300);
  Serial.println();
  Serial.println("[ili9341-test] boot");
  Serial.printf("[ili9341-test] pins cs=%d dc=%d rst=%d mosi=%d miso=%d sck=%d\n", TFT_CS, TFT_DC, TFT_RST, TFT_MOSI, TFT_MISO, TFT_SCK);

  SPI.begin(TFT_SCK, TFT_MISO, TFT_MOSI, TFT_CS);
  tft.begin(8000000);
  tft.setRotation(1);
  Serial.println("[ili9341-test] begin complete");

  label("RED", ILI9341_RED);
  delay(900);
  label("GREEN", ILI9341_GREEN);
  delay(900);
  label("BLUE", ILI9341_BLUE);
  delay(900);
}

void loop() {
  tft.fillScreen(ILI9341_BLACK);
  tft.drawRect(0, 0, 320, 240, ILI9341_WHITE);
  tft.fillRect(0, 0, 106, 240, ILI9341_RED);
  tft.fillRect(106, 0, 107, 240, ILI9341_GREEN);
  tft.fillRect(213, 0, 107, 240, ILI9341_BLUE);
  tft.drawFastVLine(160, 0, 240, ILI9341_WHITE);
  tft.setTextColor(ILI9341_WHITE);
  tft.setTextSize(2);
  tft.setCursor(10, 20);
  tft.print("LEFT");
  tft.setCursor(126, 112);
  tft.print("CENTER");
  tft.setCursor(242, 205);
  tft.print("RIGHT");
  delay(3000);
}

