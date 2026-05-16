#pragma once

#define USER_SETUP_LOADED 1
#define ILI9341_DRIVER

// Shared SPI bus for ILI9341 display and XPT2046 touch.
#define TFT_MOSI 11
#define TFT_MISO 13
#define TFT_SCLK 12
#define TFT_CS   10
#define TFT_DC   9
#define TFT_RST  8

#define LOAD_GLCD
#define LOAD_FONT2
#define LOAD_FONT4
#define LOAD_FONT6
#define LOAD_FONT7
#define LOAD_FONT8
#define LOAD_GFXFF
#define SMOOTH_FONT

#define SPI_FREQUENCY       40000000
#define SPI_READ_FREQUENCY  16000000
#define SPI_TOUCH_FREQUENCY 2500000

