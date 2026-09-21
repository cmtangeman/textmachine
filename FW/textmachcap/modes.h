#ifndef MODES_H
#define MODES_H

#include <Arduino.h>
#include <Adafruit_ILI9341.h>

// ── Pins/config this module needs — moved here from textmachcap.ino so
// modes.cpp can see them too (a sibling .cpp file doesn't see a #define
// from the .ino unless it's shared through a header like this one).
// textmachcap.ino still uses these same names, now via #include "modes.h".
#define TFT_CS         6
#define CTP_RST        5
#define SD_CS          3
#define ROTATION       4
#define SCREEN_PWR_PIN 0
#define SLEEP_PIN      1   // HIGH = sleep, LOW = wake
#define LED_SCREEN     18

// ── Shared state modes.cpp reads/writes, defined in textmachcap.ino ────
extern Adafruit_ILI9341 tft;
extern bool screenAsleep;
extern unsigned long lastTouchTime;
extern unsigned long lastCSQUpdate;
extern unsigned long lastClockUpdate;
extern volatile bool wakeRequested;

// ── Sleep/wake API ───────────────────────────────────────────────────
// The TFT, FT6336U touch controller, and SD card all live on one board
// sharing a single Vcc rail — see modes.cpp for the full rationale. Each
// "sleep" puts its IC in its lowest defined state and releases the
// SAMD21-side bus; each "wake" does a full cold-boot re-init rather than a
// light resume, since a real Vcc cut wipes every register/GRAM/card-init
// state on the board.
void screenSleep();
void screenWake();
void ctpSleep();
void ctpReset();
void sdSleep();
void sdWake();

void wakeISR();
void modemSleep();
void modemWake();
void enterSleep();
void exitAndReset();

void exitAndReset_Light();
void enterLightSleep();

#endif
