#ifndef BUTTONS_H
#define BUTTONS_H

#include <Arduino.h>
#include "types.h"



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
