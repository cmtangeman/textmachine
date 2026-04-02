#pragma once
#include <Arduino.h>

struct ScreenPoint {
  int16_t x;
  int16_t y;
  ScreenPoint(int16_t xIn = 0, int16_t yIn = 0) : x(xIn), y(yIn) {}
};

