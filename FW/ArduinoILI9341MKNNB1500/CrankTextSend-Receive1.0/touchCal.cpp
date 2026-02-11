#include "touchCal.h"
#include <XPT2046_Touchscreen.h>

// These are defined in your .ino (or elsewhere)
extern XPT2046_Touchscreen ts;

bool readTouch(ScreenPoint &sp) {
  if (!ts.touched()) return false;
  TS_Point p = ts.getPoint();
  sp = getScreenCoords(p.x, p.y);
  return true;
}
