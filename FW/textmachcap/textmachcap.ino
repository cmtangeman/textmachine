/*
  textmachcap.ino
  Touch input: FT6336U capacitive (I2C, 0x38)
  Display:     ILI9341 (SPI)
  This is the only file that handles touch within this project.
*/

// Arduino libraries 
#include "Adafruit_GFX.h" // UI rendering and extra feautres
#include "Adafruit_ILI9341.h" // Drives the rendered commands to the touch screen 
#include <MKRNB.h>  // Handles modem commands


// Arduino core 
#include "SPI.h"
#include "Wire.h"
#include "Math.h"

// C std lib
#include <stdio.h>

// Local dependencies
#include "types.h"
#include "buttons.h"
#include "messages.h"
#include "keyboard.h"

// ── Pin definitions ───────────────────────────────────────────────
#define TFT_DC   7
#define TFT_CS   6
#define TFT_RST -1

#define CTP_RST  5
#define CTP_INT  4
#define CTP_SDA  11
#define CTP_SCL  12

#define FT6336U_ADDR      0x38
#define FT_REG_NUMTOUCHES 0x02
#define FT_REG_TOUCH1     0x03

#define SD_CS    0

#define ROTATION 0

// ── Peripherals ───────────────────────────────────────────────────
Adafruit_ILI9341 tft = Adafruit_ILI9341(TFT_CS, TFT_DC, TFT_RST);


// ── App state ─────────────────────────────────────────────────────
bool menuDrawn     = false;
bool numberAquired = false;
int  displayConvo  = 0;

#define MAX_PHONE_LEN        20
#define MAX_BODY_LEN        161
#define MAX_BODY_RECEIVE_LEN 300

static char recipientNumber[MAX_PHONE_LEN];
static char msgBody[MAX_BODY_LEN];


NB nbAccess;
NB_SMS sms;
//NBModem modem;

// ── Touch ─────────────────────────────────────────────────────────
// FT6336U outputs pixel coordinates directly — no calibration needed.

bool ctpRead(ScreenPoint& sp) {
  Wire.beginTransmission(FT6336U_ADDR);
  Wire.write(FT_REG_NUMTOUCHES);
  Wire.endTransmission(false);
  Wire.requestFrom(FT6336U_ADDR, 5);  // Request 5 bytes from 0x38 

  if (Wire.available() < 5) return false; // Malfunction

  uint8_t touches = Wire.read();  // 
  if (touches == 0 || touches > 2) return false;
  // & -> bitwise AND 0's out the top nibble and keeps all 1's from bottom niblle ( onlu the bottom nibble contains screen position, top is flags etc.)
  uint16_t x = ((Wire.read() & 0x0F) << 8) | Wire.read(); // Masks upper nibble of byte and then uses bitwise OR to combine
  uint16_t y = ((Wire.read() & 0x0F) << 8) | Wire.read();

  sp = ScreenPoint((int16_t)x, (int16_t)y);
  return true;
}

// ── NB / SMS helpers ──────────────────────────────────────────────

unsigned long testFilledCircles(uint8_t radius, uint16_t color) {
  unsigned long start;
  int x, y, w = tft.width(), h = tft.height(), r2 = radius * 2;
  tft.fillScreen(ILI9341_BLACK);
  start = micros();
  for (x = radius; x < w; x += r2)
    for (y = radius; y < h; y += r2)
      tft.fillCircle(x, y, radius, color);
  return micros() - start;
}

void receive() {
  int  c;
  char senderNumber[30];
  char senderBody[500];
  char timestamp[25] = "unknown";  // default if parse fails
  int  i = 0;

  if (sms.available()) {

    // ── Grab timestamp via AT+CMGR ────────────────────────────
    SerialSARA.println("AT+CMGR=1");
    delay(300);
    String raw = "";
    while (SerialSARA.available()) {
      raw += (char)SerialSARA.read();
    }
    // +CMGR: "REC READ","+14155551234",,"26/05/07,10:30:00-28"
    int tsStart = raw.lastIndexOf(",\"") + 2;
    int tsEnd   = raw.indexOf("\"", tsStart);
    if (tsStart > 1 && tsEnd > tsStart) {
      raw.substring(tsStart, tsEnd).toCharArray(timestamp, 25);
      Serial.println(timestamp);
    }
    // ─────────────────────────────────────────────────────────

    sms.remoteNumber(senderNumber, 20);

    while ((c = sms.read()) != -1 && i < 499) {
      senderBody[i++] = (char)c;
    }
    senderBody[i] = '\0';

    pushMessage(senderNumber, senderBody, IN, timestamp);
    sms.flush();
  }
}

void text(const char* remoteNum, const char* message) {
    tft.fillScreen(ILI9341_BLACK);
    tft.setCursor(0, 0);

    // Check signal first
    SerialSARA.println("AT+CSQ");
    delay(500);
    String csq = "";
    while (SerialSARA.available()) {
        csq += (char)SerialSARA.read();
    }

    // CSQ of 99 means no signal
    if (csq.indexOf("99,99") != -1 || csq.indexOf("+CSQ: 0") != -1) {
        tft.println("No signal.");
        tft.println("Message not sent.");
        delay(1000);
        return;  // bail out before endSMS
    }

    // Signal looks good, attempt send
    tft.println("Sending...");
    sms.beginSMS(remoteNum);
    sms.print(message);
    int result = sms.endSMS();

    if (result == 1) {
        pushMessage(remoteNum, message, OUT, "Unknown");
        tft.println("Sent!");
    } else {
        tft.println("Failed.");
    }
    delay(1000);
}

// ── UI state machine ──────────────────────────────────────────────

enum UiState { UI_MENU, UI_MESSAGES, UI_REFRESH, UI_COMPOSE, UI_CONVO };
UiState currentState = UI_MENU;

// ── Setup ─────────────────────────────────────────────────────────

void setup() {
  Serial.begin(9600);

  pinMode(TFT_CS, OUTPUT);
  digitalWrite(TFT_CS, HIGH);

  // Reset touch controller
  pinMode(CTP_RST, OUTPUT);
  digitalWrite(CTP_RST, LOW);  delay(10);
  digitalWrite(CTP_RST, HIGH); delay(50);
  Wire.begin();

  tft.begin();
  tft.setRotation(ROTATION);
  tft.fillScreen(ILI9341_BLACK);
  tft.invertDisplay(true);



  bool connected = false;
while (!connected) {
    if (nbAccess.begin("") == NB_READY) {
        connected = true;
        
        // PSM low power modem
        // nbAcess begin also activates modem begin 
        // CPSMS -> Power Saving Mode Settings: 
        /*
        The command controls whether the UE wants to apply PSM or not, as well as:
        • the requested extended periodic RAU value in GERAN/UTRAN : 2g/3g legacy
        • the requested GPRS READY timer value in GERAN/UTRAN : 2g/3g legacy
        • the requested extended periodic TAU value in E-UTRAN : How long to sleep 
        * (Tracking time update)
          the requested Active Time value : How long to stay awak after activity 
                  modem.send("AT+CPSMS=1,,,\"00000001\",\"00001010\"");
//                           TAU=10min   Active=20sec
        String response = modem.receive(1000);  // Waits a 1000 ms to receive a string from the modem
        Serial.println(response);
  */
    SerialSARA.println("AT+CPSMS=0");
    // SerialSARA.println("AT+CPSMS=1,,,\"00000001\",\"00001010\"");
    delay(1000);
    while (SerialSARA.available()) {
        Serial.write(SerialSARA.read());
    }

        Serial.print(F("Circles (filled)         "));
        Serial.println(testFilledCircles(10, ILI9341_BLUE));
    } else {
        tft.println("Not connected");
        delay(1000);
    }
}
}

// ── Loop ──────────────────────────────────────────────────────────

void loop() {
  static Button msgBtn, compBtn, refreshBtn, contactsBtn, backBtn;
  static bool isDrawnConvo = false;

  if (!menuDrawn) {
    tft.setTextColor(ILI9341_WHITE);
    tft.setTextSize(2);
    tft.fillScreen(ILI9341_BLACK);
    tft.setRotation(ROTATION);

    msgBtn.initButton(0,  60, 240, 40, "Messages");
    compBtn.initButton(0, 110, 240, 40, "Compose");
    refreshBtn.initButton(0, 160, 240, 40, "Refresh");
    contactsBtn.initButton(0, 210, 240, 40, "Contacts");

    menuDrawn = true;
  }

  bool touched = false;
  ScreenPoint sp;

  if (ctpRead(sp)) {
    touched = true;
  }

  static bool wasTouched = false;
  bool justPressed = touched && !wasTouched;
  wasTouched = touched;

  switch (currentState) {

    case UI_MENU: {
      if (justPressed && msgBtn.isClicked(sp)) {
        currentState = UI_MESSAGES;
        return;
      }
      if (justPressed && compBtn.isClicked(sp)) {
        currentState = UI_COMPOSE;
        keyboardReset();
        return;
      }
      if (justPressed && refreshBtn.isClicked(sp)) {
        tft.fillScreen(ILI9341_BLACK);
        tft.setCursor(0, 0);
        receive();
        while (ctpRead(sp)) delay(10);
        currentState = UI_MENU;
        menuDrawn = false;
        return;
      }
      if (justPressed && contactsBtn.isClicked(sp)) {
        tft.fillScreen(ILI9341_ORANGE);
        tft.setCursor(0, 0);
        tft.println("Other button touched");
        menuDrawn = false;
        return;
      }
      break;
    }

    case UI_MESSAGES: {
      recentMessagesScreen(sp, false);

      if (justPressed && msgBackBtnPressed(sp)) {
        recentMessagesReset();
        currentState = UI_MENU;
        menuDrawn = false;
        wasTouched = true;
        return;
      }

      int picked = recentMessagesScreen(sp, justPressed);
      if (picked != -1) {
        displayConvo = picked;
        conversationReset();
        currentState = UI_CONVO;
        wasTouched = true;
        return;
      }
      break;
    }

    case UI_CONVO: {
      drawConversationToTFT(displayConvo);

      if (justPressed && convoBackBtnPressed(sp)) {
        conversationReset();
        recentMessagesReset();
        currentState = UI_MESSAGES;
        wasTouched = true;
        return;
      }
      break;
    }

    case UI_COMPOSE: {
      if (!numberAquired) {
        if (justPressed && keyboardBackPressed(sp)) {
          currentState = UI_MENU;
          menuDrawn = false;
          return;
        }
        if (justPressed && msgBtnPressed(sp)) {
          const char* kb = keyboardGetText();
          strncpy(recipientNumber, kb, MAX_PHONE_LEN - 1);
          recipientNumber[MAX_PHONE_LEN - 1] = '\0';
          numberAquired = true;
          keyboardClearText();
          Serial.println("Phone # acquired");
        } else if (keyboardTick(sp, justPressed)) {
          Serial.println("Error: Send pressed with no number");
        }
      } else {
        if (justPressed && keyboardBackPressed(sp)) {
          currentState = UI_MENU;
          menuDrawn = false;
          return;
        } else if (justPressed && toBtnPressed(sp)) {
          numberAquired = false;
          keyboardReset();
        } else if (keyboardTick(sp, justPressed)) {
          const char* kb2 = keyboardGetText();
          strncpy(msgBody, kb2, MAX_BODY_LEN - 1);
          msgBody[MAX_BODY_LEN - 1] = '\0';
          text(recipientNumber, msgBody);
          Serial.println("TextSent");
          numberAquired = false;
          keyboardReset();
          currentState = UI_MENU;
          menuDrawn = false;
          return;
        }
      }
      break;
    }

    default: break;
  }
}