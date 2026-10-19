#ifndef UI_H
#define UI_H

#include "types.h"

#include <stdint.h>

// Shared dark-minimalist palette — same visual language as the splash
// screen and keyboard.cpp's own dark palette (COL_KB_BG/COL_SEND/etc,
// kept as a separate copy since keyboard.cpp draws before uiInitColors()
// is guaranteed to have run, but COL_SEND is set to the same Android green
// as UI_ACCENT below so the two stay visually in sync).
// These are populated at runtime by uiInitColors() (tft.color565() needs
// the tft object constructed, so they can't be plain compile-time consts
// the way ILI9341_BLACK etc. are) — call it once from setup() before any
// screen is drawn.
extern uint16_t UI_BG;        // app background — near-black
extern uint16_t UI_SURFACE;   // row/card background — one step up from UI_BG
extern uint16_t UI_BORDER;    // hairline dividers between rows
extern uint16_t UI_TEXT;      // primary text — white
extern uint16_t UI_TEXT_DIM;  // secondary text — timestamps, phone numbers, metadata
extern uint16_t UI_ACCENT;    // Android green — primary actions, selection, links
extern uint16_t UI_DANGER;    // delete / destructive — muted red, used sparingly
extern uint16_t UI_FIELD;     // input fields — white

void uiInitColors();

void uiUseDefaultFont();
void uiUseButtonFont();
void uiUseTitleFont();

bool  islandIsClicked(const ScreenPoint& sp);

// Rounded pill with a centered chevron — the swipe/reveal handle shared by
// the home menu (pointing up, opens Settings) and the Settings screen
// (pointing down, closes it back to the menu). Purely a draw call; the
// caller owns the actual Button/tap-target at the same (x,y,w,h).
void drawSwipeIndicator(int x, int y, int w, int h, bool pointDown);


void updateClock();
void updateCSQ();
void updateBattery();

// Formats `raw` (a modem-format timestamp, "yy/MM/dd,hh:mm:ss±zz" — the same
// shape AT+CCLK? and a CMGL line's quoted timestamp field both use) for
// display. `nowRaw` is a timestamp in that same raw format representing
// "now" (e.g. straight from getCurrentRawTimestamp()) — used only to decide
// how old `raw` is, not displayed itself. Within the last 7 days: weekday +
// time ("Wed 14:32"). 7+ days ago: date only, no leading zeros ("9/5/2026")
// — there's no relative label (like "Yesterday") for anything in between,
// just the weekday name. Pass nowRaw == raw (or null) to always take the
// recent-weekday branch — e.g. when formatting "now" itself.
void formatTimestamp(const char* raw, const char* nowRaw, char* output, int outLen);

// Just the time, 12-hour with AM/PM, no leading zero on the hour — e.g.
// "2:23 PM". `raw` is the same modem format formatTimestamp() takes. Used
// internally by formatFullTimestamp() below. Falls back to printing `raw`
// as-is if it doesn't parse (e.g. empty, for a message with no raw
// timestamp available — see Msg::hasRawTime in messages.h).
void formatMessageTime(const char* raw, char* output, int outLen);

// Weekday + full date + time together ("Wed 9/5/2026 2:23 PM"), no
// age-based branching — for a held-and-flipped message bubble's "back"
// face, where the point is showing everything regardless of how old the
// message is. `raw` is the same modem format formatTimestamp() takes.
// Falls back to printing `raw` as-is if it doesn't parse.
void formatFullTimestamp(const char* raw, char* output, int outLen);

// Raw AT+CCLK? read, unformatted ("yy/MM/dd,hh:mm:ss±zz") — same shape
// formatTimestamp() expects for both its `raw` and `nowRaw` parameters.
// Returns false (output untouched) if the modem didn't return a usable time.
bool getCurrentRawTimestamp(char* output, int outLen);

// Queries the modem's clock (AT+CCLK?) and formats it the same way formatTimestamp()
// does for received messages. Returns false (output untouched) if the modem didn't
// return a usable time.
bool getCurrentTimestamp(char* output, int outLen);

#endif