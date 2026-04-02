#include "buttons.h"

#include <math.h>
#include <Adafruit_ILI9341.h>
#include <Adafruit_GFX.h>
#include <Fonts/FreeSans9pt7b.h>
#include <Fonts/FreeSansBold9pt7b.h>


extern Adafruit_ILI9341 tft;

Button::Button()
  : x(0), y(0), width(0), height(0), text(nullptr) {}

void Button::initButton(int xPos, int yPos, int butWidth, int butHeight, const char* butText, uint16_t butColor) {
  x = xPos;
  y = yPos;
  width = butWidth;
  height = butHeight;
  text = butText;
  color = butColor;
  render();
}

static Button backBtn;

  void drawBackBtn(){
  backBtn.initButton(0, 0, 36, 36, "<");
  }

void Button::render() {
  tft.fillRect(x, y, width, height, color);

  tft.setFont(&FreeSans9pt7b);   // use GFX font
  tft.setTextSize(1);            // must stay 1 for GFX fonts
  tft.setTextColor(ILI9341_WHITE);

  // center text better
  int cursorX = x + 6;
  int cursorY = y + height/2 + 4;

  tft.setCursor(cursorX, cursorY);
  tft.print(text);

  tft.setFont(NULL);   // restore default for rest of UI
}

bool Button::isClicked(const ScreenPoint& sp) {
  return (sp.x >= x && sp.x <= (x + width) && sp.y >= y && sp.y <= (y + height));
}


