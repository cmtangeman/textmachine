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



void Button::render() {
  tft.fillRect(x, y, width, height, ILI9341_MAGENTA);
  tft.setCursor(x + 5, y + 5);
  tft.setTextSize(2);
  tft.setTextColor(ILI9341_WHITE);
  tft.print(text);
}

bool Button::isClicked(const ScreenPoint& sp) {
  return (sp.x >= x && sp.x <= (x + width) && sp.y >= y && sp.y <= (y + height));
}

ScreenPoint getScreenCoords(int16_t rawX, int16_t rawY) {
  int16_t xCoord = (int16_t)roundf((rawX * X_CAL_M) + X_CAL_C);
  int16_t yCoord = (int16_t)roundf((rawY * Y_CAL_M) + Y_CAL_C);

  if (xCoord < 0) xCoord = 0;
  if (yCoord < 0) yCoord = 0;
  if (xCoord >= (int16_t)tft.width())  xCoord = tft.width() - 1;
  if (yCoord >= (int16_t)tft.height()) yCoord = tft.height() - 1;

  return { xCoord, yCoord };
}
