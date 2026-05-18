#ifndef TYPES_H
#define TYPES_H

#include <Arduino.h>

// ── Display ───────────────────────────────────────────────────────

struct ScreenPoint {
  int16_t x;
  int16_t y;
  ScreenPoint(int16_t x = 0, int16_t y = 0) : x(x), y(y) {}
};


#endif // TYPES_H