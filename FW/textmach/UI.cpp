#include "ui.h"
#include <Adafruit_GFX.h>
#include <Fonts/FreeSans9pt7b.h>
#include <Fonts/FreeSansBold9pt7b.h>
#include <Adafruit_ILI9341.h>

extern Adafruit_ILI9341 tft;

const uint16_t UI_BG     = ILI9341_BLACK;
const uint16_t UI_KEY    = ILI9341_DARKGREY;
const uint16_t UI_TEXT   = ILI9341_WHITE;
const uint16_t UI_ACCENT = ILI9341_BLUE;
const uint16_t UI_FIELD  = ILI9341_WHITE;

void uiUseDefaultFont() {
  tft.setFont(NULL);
  tft.setTextSize(2);
}

void uiUseButtonFont() {
  tft.setFont(&FreeSans9pt7b);
  tft.setTextSize(1);
}