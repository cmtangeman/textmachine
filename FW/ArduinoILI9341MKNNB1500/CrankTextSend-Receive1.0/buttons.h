#ifndef BUTTONS_H
#define BUTTONS_H

#include <Arduino.h>

struct ScreenPoint {
  int16_t x;
  int16_t y;
};

constexpr float X_CAL_M = -0.09f;
constexpr float Y_CAL_M = -0.06f;
constexpr float X_CAL_C = 333.49f;
constexpr float Y_CAL_C = 250.6f;

ScreenPoint getScreenCoords(int16_t rawX, int16_t rawY);


class Button {
public:
  int x;
  int y;
  int width;
  int height;
  const char* text;

  Button();
  void initButton(int xPos, int yPos, int butWidth, int butHeight, const char* butText);
  void render();
  bool isClicked(const ScreenPoint& sp);
};


#endif // BUTTONS_H
