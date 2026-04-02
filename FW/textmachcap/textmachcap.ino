/*
  textmachcap.ino
  Touch input: FT6336U capacitive (I2C, 0x38)
  Display:     ILI9341 (SPI)
  This is the only file that handles touch within this project.
*/

#include "Adafruit_GFX.h"
#include "Adafruit_ILI9341.h"
#include "SPI.h"
#include "Wire.h"
#include "Math.h"

// Local dependencies
#include "types.h"
#include "buttons.h"
#include "messages.h"
#include "keyboard.h"

// ── Pin definitions ───────────────────────────────────────────────
#define TFT_DC   6
#define TFT_CS   7
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

#include <MKRNB.h>
#include <stdio.h>
NB     nbAccess;
NB_SMS sms;

// ── Touch ─────────────────────────────────────────────────────────
// FT6336U outputs pixel coordinates directly — no calibration needed.

bool ctpRead(ScreenPoint& sp) {
  Wire.beginTransmission(FT6336U_ADDR);
  Wire.write(FT_REG_NUMTOUCHES);
  Wire.endTransmission(false);
  Wire.requestFrom(FT6336U_ADDR, 5);

  if (Wire.available() < 5) return false;

  uint8_t touches = Wire.read();
  if (touches == 0 || touches > 2) return false;

  uint16_t x = ((Wire.read() & 0x0F) << 8) | Wire.read();
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
  int  i = 0;

  if (sms.available()) {
    Serial.println("Message received from:");
    tft.println("-----------------");
    tft.println("Message received from:");

    sms.remoteNumber(senderNumber, 20);
    Serial.println(senderNumber);
    tft.println(senderNumber);

    while ((c = sms.read()) != -1 && i < 499) {
      senderBody[i++] = (char)c;
      Serial.print((char)c);
      tft.print((char)c);
    }
    senderBody[i] = '\0';

    pushMessage(senderNumber, senderBody, IN);
    tft.println();
    tft.println("-----------------");
    sms.flush();
    Serial.println("MESSAGE DELETED FROM MODEM MEMORY SAVED IN FW/");
  } else {
    Serial.println("No new messages");
    tft.println("No new messages");
  }
  delay(500);
}

void text(const char* remoteNum, const char* message) {
  sms.beginSMS(remoteNum);
  sms.print(message);
  sms.endSMS();
  pushMessage(remoteNum, message, OUT);
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