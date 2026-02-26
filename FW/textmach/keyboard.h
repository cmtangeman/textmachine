#ifndef KEYBOARD_H
#define KEYBOARD_H

struct ScreenPoint;

bool keyboardTick(const ScreenPoint& sp, bool justTouched);
const char* keyboardGetText(void);
void keyboardReset(void);
void keyboardClearText(void);
static void drawKeyboard(void);

// NEW: lets your UI state handle back without changing keyboardTick() return type
bool keyboardBackPressed(const ScreenPoint& sp);
bool msgBtnPressed(const ScreenPoint& sp);
bool toBtnPressed(const ScreenPoint& sp);

#endif
