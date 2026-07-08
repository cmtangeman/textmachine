#ifndef BUTTONS_H
#define BUTTONS_H

#include <Arduino.h>
#include "types.h"
#include "messages.h"



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

class msgButton : public Button { // inheriting from Button
public:
    MsgDir dir;
    bool kept;  // shows a small "kept" indicator when tapped on, like Snapchat's keep marker
    void initMsgButton(int xPos, int yPos, int butWidth, int butHeight,
                       const char* msg, MsgDir direction, bool isKept);
    void render();
};


#endif // BUTTONS_H
