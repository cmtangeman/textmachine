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
  uint16_t color;

  void drawBackBtn();
  Button();
  void initButton(int xPos, int yPos, int butWidth, int butHeight, const char* butText, uint16_t butColor = 0x4228);
  // If nothing passed just uses default color
  void render();
  bool isClicked(const ScreenPoint& sp);
  void drawBackButton();
};


#endif // BUTTONS_H
