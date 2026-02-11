#pragma once
#include <Arduino.h>
#include "buttons.h"     // for ScreenPoint

ScreenPoint getScreenCoords(int16_t rawX, int16_t rawY);
bool readTouch(ScreenPoint &sp);
