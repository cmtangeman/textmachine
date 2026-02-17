#include "buttons.h"

#include <math.h>
#include <Adafruit_ILI9341.h>

extern Adafruit_ILI9341 tft;

Button::Button()
  : x(0), y(0), width(0), height(0), text(nullptr) {}

void Button::initButton(int xPos, int yPos, int butWidth, int butHeight, const char* butText) {
  x = xPos;
  y = yPos;
  width = butWidth;
  height = butHeight;
  text = butText;
  render();
}

static Button backBtn;

  void drawBackBtn(){
  backBtn.initButton(0, 0, 36, 36, "<");
  }

void Button::render() {
  tft.fillRect(x, y, width, height, ILI9341_BLUE);
  tft.setCursor(x + 5, y + 5);
  tft.setTextSize(2);
  tft.setTextColor(ILI9341_WHITE);
  tft.print(text);
}

bool Button::isClicked(const ScreenPoint& sp) {
  return (sp.x >= x && sp.x <= (x + width) && sp.y >= y && sp.y <= (y + height));
}


