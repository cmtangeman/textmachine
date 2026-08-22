/*#ifndef MODES_H
#define MODES_H

#include <Arduino.h>

enum RadioState { RADIO_ASLEEP, RADIO_AWAKE, RADIO_WAKING };

extern RadioState radioState;
extern volatile bool radioBusy;

void mcuSleep();
void mcuWake();

bool wakeRadio();   // exits PSM, polls AT+CEREG? until registered or timeout
void sleepRadio();  // re-enters PSM

void doRadioOperation(void (*operation)());

#endif
*/