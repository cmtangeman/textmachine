#ifndef UI_H
#define UI_H

#include <stdint.h>

extern const uint16_t UI_BG;
extern const uint16_t UI_KEY;
extern const uint16_t UI_TEXT;
extern const uint16_t UI_ACCENT;
extern const uint16_t UI_FIELD;

void uiUseDefaultFont();
void uiUseButtonFont();


void updateClock();
void updateCSQ();

#endif