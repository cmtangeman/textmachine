#ifndef KEYBOARD_H
#define KEYBOARD_H

struct ScreenPoint;

bool keyboardTick(const ScreenPoint& sp, bool justTouched, int mode);
const char* keyboardGetText(void);
void keyboardReset(void);
void keyboardForceRedraw(void);
void keyboardClearText(void);
void keyboardSwitchToMessageField(const char* keepInToField);
static void drawKeyboard(void);
static void drawNumberpad(void);

// NEW: lets your UI state handle back without changing keyboardTick() return type
bool keyboardBackPressed(const ScreenPoint& sp);
bool msgBtnPressed(const ScreenPoint& sp);
bool toBtnPressed(const ScreenPoint& sp);
bool nameBtnPressed(const ScreenPoint& sp);
bool numberBtnPressed(const ScreenPoint& sp);

void debugPrint(const char* cmd, const char* response);

#endif
