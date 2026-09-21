#ifndef SETTINGS_H
#define SETTINGS_H

#include "types.h"

// Draws (once per settingsScreenReset()) and handles taps for the Settings
// screen: the downward swipe indicator that closes it back to the menu,
// the Contacts Only Mode toggle (visual-only for now — no backend wiring
// yet, just an on/off flag), and the brightness slider (drives the real
// `brightness` global textmachcap.ino/modes.cpp use for the backlight PWM).
void drawSettingsScreen(const ScreenPoint& sp, bool justPressed);

// True the instant the close (downward-pointing) pill is tapped — caller
// transitions back to UI_MENU when this fires, same pattern as
// msgBackBtnPressed()/convoBackBtnPressed() elsewhere.
bool settingsCloseBtnPressed(const ScreenPoint& sp);

void settingsScreenReset();

#endif
