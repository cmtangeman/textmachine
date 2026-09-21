#ifndef KEYBOARD_H
#define KEYBOARD_H

struct ScreenPoint;

bool keyboardTick(const ScreenPoint& sp, bool justTouched, int mode);
const char* keyboardGetText(void);
void keyboardReset(void);
void keyboardForceRedraw(void);
void keyboardClearText(void);
void keyboardSwitchToMessageField(const char* keepInToField, int mode);
static void drawKeyboard(void);
static void drawNumberpad(void);

// NEW: lets your UI state handle back without changing keyboardTick() return type
bool keyboardBackPressed(const ScreenPoint& sp);
bool msgBtnPressed(const ScreenPoint& sp);
bool toBtnPressed(const ScreenPoint& sp);
bool nameBtnPressed(const ScreenPoint& sp);
bool numberBtnPressed(const ScreenPoint& sp);

// Top-right rotate button, compose screen only (see drawTextHeader()) —
// flips the whole display between portrait and landscape for texting.
// Only meaningful while operatingMode == KB_COMPOSE; the button isn't
// drawn/clickable in the other keyboard modes.
bool rotateBtnPressed(const ScreenPoint& sp);

// Flips tft.setRotation() between portrait (modes.h's ROTATION) and
// landscape, forces a full keyboard redraw at the new dimensions, and
// updates the flag keyboardIsLandscape() reports. Called from
// textmachcap.ino when rotateBtnPressed() fires.
void keyboardToggleRotation(void);

// True while the display is currently rotated to landscape for the
// compose screen — textmachcap.ino's ctpRead() needs this at runtime to
// remap touch coordinates correctly, since the FT6336U always reports in
// the panel's native (portrait) coordinate space regardless of
// tft.setRotation(). keyboardReset() always restores portrait, so this is
// false again the moment compose is left.
bool keyboardIsLandscape(void);

void debugPrint(const char* cmd, const char* response);

#endif
