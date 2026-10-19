#include "buttons.h"

#include <math.h>
#include <Adafruit_ILI9341.h>
#include <Adafruit_GFX.h>
#include <Fonts/FreeSans9pt7b.h>
#include <Fonts/FreeSansBold9pt7b.h>
#include "UI.h"


extern Adafruit_ILI9341 tft;

Button::Button()
  : x(0), y(0), width(0), height(0), text(nullptr) {}

void Button::initButton(int xPos, int yPos, int butWidth, int butHeight, const char* butText, uint16_t butColor) {
  x = xPos;
  y = yPos;
  width = butWidth;
  height = butHeight;
  text = butText;
  color = butColor;
  render();
}

static Button backBtn;

  void drawBackBtn(){
  backBtn.initButton(0, 0, 36, 36, "<");
  }

void Button::render() {
  // FIX: this used to hardcode ILI9341_BLACK here, so `color` (set by
  // initButton) was silently ignored and every button in the app rendered
  // identically regardless of what color it was given.
  tft.fillRoundRect(x, y, width, height, 6, color);

  tft.setFont(&FreeSans9pt7b);   // use GFX font
  tft.setTextSize(1);            // must stay 1 for GFX fonts
  tft.setTextColor(UI_TEXT);

  // center text better
  int cursorX = x + 8;
  int cursorY = y + height/2 + 4;

  tft.setCursor(cursorX, cursorY);
  tft.print(text);

  tft.setFont(NULL);   // restore default for rest of UI
}

bool Button::isClicked(const ScreenPoint& sp) {
  return (sp.x >= x && sp.x <= (x + width) && sp.y >= y && sp.y <= (y + height));
}

void msgButton::initMsgButton(int xPos, int yPos, int butWidth, int butHeight,
                               const char* msg, MsgDir direction, bool isKept) {
  x = xPos;
  y = yPos;
  width = butWidth;
  height = butHeight;
  text = msg;
  dir = direction;
  kept = isKept;
  render();
}

void msgButton::render() {
    uint16_t bubbleColor = (dir == OUT)
        ? UI_ACCENT                        // Android green for outgoing
        : tft.color565(54, 54, 57);        // cool dark grey for incoming — a touch
                                            // lighter than UI_SURFACE so bubbles read
                                            // clearly against the plain background

    tft.fillRoundRect(x, y, width, height, 8, bubbleColor);
    tft.setTextColor(UI_TEXT);
    tft.setTextSize(1);

    const int padding    = 6;
    const int lineHeight = 10;

    char lines[MAX_MSG_RENDER_LINES][MAX_BODY_LEN];
    int lineCount = wrapMessageText(text, width - 2 * padding, lines, MAX_MSG_RENDER_LINES);
    if (lineCount > MAX_MSG_RENDER_LINES) lineCount = MAX_MSG_RENDER_LINES;

    for (int i = 0; i < lineCount; i++) {
        tft.setCursor(x + padding, y + padding + i * lineHeight);
        tft.print(lines[i]);
    }

    // "Kept" indicator — traces the whole bubble in yellow once you tap it,
    // instead of a corner dot, so it reads at a glance regardless of how
    // many lines the bubble wrapped to. Two concentric rounded rects for a
    // visible 2px stroke (Adafruit_GFX's drawRoundRect is 1px).
    if (kept) {
        tft.drawRoundRect(x,     y,     width,     height,     8, ILI9341_YELLOW);
        tft.drawRoundRect(x + 1, y + 1, width - 2, height - 2, 7, ILI9341_YELLOW);
    }
}





