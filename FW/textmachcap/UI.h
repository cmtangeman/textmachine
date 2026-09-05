#ifndef UI_H
#define UI_H

#include <stdint.h>

// Shared dark-minimalist palette — same visual language as the splash
// screen and keyboard.cpp's existing dark palette (COL_KB_BG/COL_SEND/etc,
// left as-is since compose/add-contact are out of scope for this pass).
// These are populated at runtime by uiInitColors() (tft.color565() needs
// the tft object constructed, so they can't be plain compile-time consts
// the way ILI9341_BLACK etc. are) — call it once from setup() before any
// screen is drawn.
extern uint16_t UI_BG;        // app background — near-black
extern uint16_t UI_SURFACE;   // row/card background — one step up from UI_BG
extern uint16_t UI_BORDER;    // hairline dividers between rows
extern uint16_t UI_TEXT;      // primary text — white
extern uint16_t UI_TEXT_DIM;  // secondary text — timestamps, phone numbers, metadata
extern uint16_t UI_ACCENT;    // iOS blue — primary actions, selection, links
extern uint16_t UI_DANGER;    // delete / destructive — muted red, used sparingly
extern uint16_t UI_FIELD;     // input fields — white

void uiInitColors();

void uiUseDefaultFont();
void uiUseButtonFont();
void uiUseTitleFont();


void updateClock();
void updateCSQ();
void updateBattery();

void formatTimestamp(const char* raw, char* output, int outLen);

// Queries the modem's clock (AT+CCLK?) and formats it the same way formatTimestamp()
// does for received messages. Returns false (output untouched) if the modem didn't
// return a usable time.
bool getCurrentTimestamp(char* output, int outLen);

#endif