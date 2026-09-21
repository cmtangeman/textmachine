#include "modes.h"
#include <ArduinoLowPower.h>
#include <Adafruit_ILI9341.h>  // for tft.sendCommand()/begin()/setRotation()/invertDisplay()
#include <Wire.h>               // for Wire.begin()/end() — FT6336U touch controller reset
#include <SD.h>                 // for SD.begin()/end() — SD card power cycling
#include <MKRNB.h>              // for SerialSARA (the modem's UART) and AT command I/O

// Defined in textmachcap.ino; not exposed through a header there, so
// declared here the same way UI.cpp does for its own use of this helper.
extern String readModemResponse(unsigned long timeout_ms);


extern int brightness;

 
 
  // The TFT, FT6336U touch controller, and SD card all live on one board
  // sharing a single Vcc rail — the eventual plan is to gate that rail with
  // a MOSFET. These four sleep/wake pairs are written as if Vcc really is
  // cut on every sleep: each "sleep" sends its IC to its lowest defined
  // state and releases the SAMD21-side bus so nothing is mid-transaction
  // when power drops, and each "wake" re-runs a full cold-boot init rather
  // than a light resume, since a real Vcc cut would wipe every register,
  // GRAM, and card-init state on the board. SAMD21 RAM/app state persists
  // across this (it's never depowered), which is what lets us redraw
  // whatever screen was on-screen instead of resetting to the menu.
  


void screenSleep(){
  if (screenAsleep) return;
  tft.sendCommand(0x10);
  delay(5);
  pinMode(LED_SCREEN, OUTPUT);     // force pin mux back to GPIO, disconnect from PWM timer
  digitalWrite(LED_SCREEN, HIGH);  // now actually takes effect
  digitalWrite(TFT_CS, HIGH);
  screenAsleep = true;
}

void screenWake(){
  if (!screenAsleep) return;
  tft.begin();                 // full cold re-init — registers/GRAM are gone
  tft.setRotation(ROTATION);
  // tft.invertDisplay(true);
  screenAsleep = false;
  lastTouchTime = millis();
  analogWrite(LED_SCREEN, brightness);
  // digitalWrite(LED_SCREEN, LOW); // keeps LED_SCREEN, Vcc 
}

void ctpSleep() {
    digitalWrite(CTP_RST, LOW);  // hold in reset = disabled
    Wire.end();                  // release the SAMD21 side of the I2C bus too
}                                 // FIX: this brace was missing — ctpReset() was
                                  // nested inside ctpSleep(), which doesn't compile
void ctpReset() {
    Wire.begin();
    digitalWrite(CTP_RST, LOW);
    delay(5);
    digitalWrite(CTP_RST, HIGH);
    delay(50);
}


void sdSleep() {
    SD.end();                    // releases SPI bus
    pinMode(SD_CS, OUTPUT);
    digitalWrite(SD_CS, HIGH);  // deselect
}

void sdWake() {
    SD.begin(SD_CS);             // full re-init — card was fully unpowered
}

volatile bool wakeRequested = false;

void wakeISR() {
    wakeRequested = true;
}

void modemSleep() {
  SerialSARA.println("AT+UPSV=4");        // Enable power saving
  

  String ping = readModemResponse(1000);
  Serial.print("Modem response: ");
  Serial.println(ping);
}

void modemWake() {
  SerialSARA.println("AT+UPSV=0");        // Disable power saving

  String ping = readModemResponse(1000);
  Serial.print("Modem response: ");
  Serial.println(ping);

  // Have to wake it up with the initial AT command !

  SerialSARA.println("AT+UPSV=0");        // Disable power saving
  
  ping = readModemResponse(1000);
  Serial.print("Modem response: ");
  Serial.println(ping);
}

void enterLightSleep() {
    modemSleep();
    screenSleep();
    sdSleep();


}

void exitAndReset_Light(){
    //digitalWrite(SCREEN_PWR_PIN, LOW);   // transistor ON → screen powered
    // delay(50);


    // ctpReset();
    screenWake();
    sdWake();

    modemWake();

}

void enterSleep() {
    modemSleep();
    screenSleep();
    ctpSleep();
    sdSleep();

    wakeRequested = false;
    LowPower.attachInterruptWakeup(SLEEP_PIN, wakeISR, FALLING);

    digitalWrite(SCREEN_PWR_PIN, HIGH);

    LowPower.deepSleep();

    if (wakeRequested) exitAndReset();
}



void exitAndReset() {
    digitalWrite(SCREEN_PWR_PIN, LOW);   // transistor ON → screen powered
    delay(50);

    // Cold-boot the screen board back up, same relative order as setup():
    // touch (I2C) → panel (SPI) → SD (SPI). Backlight stays off through all
    // of it — screenWake() re-inits the panel but doesn't touch LED_SCREEN.
    ctpReset();
    screenWake();
    sdWake();
    

    lastTouchTime   = millis();
    lastCSQUpdate   = millis();
    lastClockUpdate = millis();

    modemWake();
}
