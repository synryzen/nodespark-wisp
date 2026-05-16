#pragma once

#define USER_SETUP_LOADED 1
#define ILI9341_DRIVER

// Shared SPI bus for ILI9341 display and XPT2046 touch.
#define TFT_MOSI 11
#define TFT_MISO 13
#define TFT_SCLK 12
#define TFT_CS   10
#define TFT_DC   9
// Tie the display RESET/RST pin to 3V3 for the first hardware bring-up. This
// avoids boot-sensitive GPIO/reset timing problems on mixed ESP32-S3 TFT modules.
#define TFT_RST  -1

#define LOAD_GLCD
#define LOAD_FONT2
#define LOAD_FONT4
#define LOAD_FONT6
#define LOAD_FONT7
#define LOAD_FONT8
#define LOAD_GFXFF
#define SMOOTH_FONT

// Keep this conservative for jumper-wire prototypes. Raise later after the
// display is confirmed stable.
#define SPI_FREQUENCY       10000000
#define SPI_READ_FREQUENCY  8000000
#define SPI_TOUCH_FREQUENCY 2500000
