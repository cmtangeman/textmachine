/*#include "modes.h"
#include "ArduinoLowPower.h"
// whatever else you need — SerialSARA, readModemResponse, etc.

RadioState radioState = RADIO_ASLEEP;
volatile bool radioBusy = false;

bool wakeRadio() {
    radioState = RADIO_WAKING;
    // ... AT+CEREG? poll loop from earlier
    radioState = success ? RADIO_AWAKE : RADIO_ASLEEP;
    return success;
}

void sleepRadio() {
    // AT+CPSMS entry / whatever your PSM re-entry sequence is
    radioState = RADIO_ASLEEP;
}

void doRadioOperationso) {
    if (radioBusy) return;
    radioBusy = true;
    if (wakeRadio()) {
        operation();
        sleepRadio();
    }
    radioBusy = false;
}
*/