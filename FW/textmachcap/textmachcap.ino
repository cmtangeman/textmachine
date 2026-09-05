/*
  textmachcap.ino
  Touch input: FT6336U capacitive (I2C, 0x38)
  Display:     ILI9341 (SPI)
  This is the only file that handles touch within this project.
*/

// Arduino libraries
#include "Adafruit_GFX.h"      // UI rendering and extra feautres
#include "Adafruit_ILI9341.h"  // Drives the rendered commands to the touch screen
#include <Fonts/FreeSans9pt7b.h>
#include <MKRNB.h>             // Handles modem commands


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
#include "contacts.h"
#include "UI.h"
#include "splash_logo.h"

// ── Pin definitions ───────────────────────────────────────────────
#define TFT_DC 7
#define TFT_CS 6
#define TFT_RST -1

#define CTP_RST 5
#define CTP_INT 4
#define CTP_SDA 11
#define CTP_SCL 12

#define FT6336U_ADDR 0x38
#define FT_REG_NUMTOUCHES 0x02
#define FT_REG_TOUCH1 0x03

#include <SD.h> // system library rather than looking for local file. 
#include "contacts.h"

static bool kbDrawnFlag = false;
enum ContactStep { CONTACT_NAME,
                   CONTACT_PHONE };
ContactStep contactStep = CONTACT_NAME;
static char newContactName[30];
static char newContactPhone[MAX_PHONE_LEN];

#define SD_CS 3




#define ROTATION 2

// ── Peripherals ───────────────────────────────────────────────────
Adafruit_ILI9341 tft = Adafruit_ILI9341(TFT_CS, TFT_DC, TFT_RST);


// ── App state ─────────────────────────────────────────────────────
bool menuDrawn = false;
bool numberAquired = false;
int displayConvo = 0;



static char recipientNumber[MAX_PHONE_LEN];
static char msgBody[MAX_BODY_LEN];


unsigned long lastCSQUpdate = 0;
unsigned long lastClockUpdate = 0;

#define CSQ_INTERVAL 300000    // 5 minutes
#define CLOCK_INTERVAL 100000  // 1 min

NB nbAccess;
NB_SMS sms;
//NBModem modem;


// LOW POWER MODE STUFF 

#include <ArduinoLowPower.h>


#define PWR_ON_PULSE_MS 250 // stay well inside the 150-3200ms window, away from the power-off overlap zone

//FOR TOUCH SLEEP TIME. 
#define SLEEP_PIN    1   // HIGH = sleep, LOW = wake


#define FT_REG_POWER_MODE  0xA5
#define FT_POWER_ACTIVE    0x00
#define FT_POWER_MONITOR   0x01  // low power, still detects touch
#define FT_POWER_HIBERNATE 0x03  // full sleep

//FOR SCREEN SLEEP TIME. 

#define LED_SCREEN 2

#define SCREEN_SLEEP_TIMEOUT 30000UL  // 30s

unsigned long lastTouchTime = 0;
bool screenAsleep = false;

// Hoisted out of loop() so exitAndReset() can clear stale debounce state
// left over from the instant sleep was entered.
bool wasTouched = false;

#define CTP_WAKE_TIMEOUT_MS 300

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

  // Remap raw touch coords to match current display rotation.
  // FT6336U always reports in native (rotation 0) panel space.
#if ROTATION == 2
  x = 240 - 1 - x;   // ILI9341 native width
  y = 320 - 1 - y;   // ILI9341 native height
#elif ROTATION == 1
  uint16_t tmp = x;
  x = y;
  y = 240 - 1 - tmp;
#elif ROTATION == 3
  uint16_t tmp = x;
  x = 320 - 1 - y;
  y = tmp;
#endif
  // ROTATION == 0 needs no remap

  sp = ScreenPoint((int16_t)x, (int16_t)y);
  return true;
}



// ── NB / SMS helpers ──────────────────────────────────────────────

// Set everything in terms of E. 164 the standard format for international telephone numbers. 
// Correct U.S not using +1 or 1 and otherwise treat as EUROPEAN number with correct country code
// Ignore + input for all and concatenate to first digit for all as well.

String readModemResponse(unsigned long timeout_ms = 2000) { // default is 2 seconds remember millis is the inner clock
    String resp = "";
    unsigned long start = millis();
    unsigned long lastByte = millis();

    while (millis() - start < timeout_ms) {
        if (SerialSARA.available()) {
            char c = (char)SerialSARA.read();
            resp += c;
            lastByte = millis();
        } else if (resp.length() > 0 && millis() - lastByte > 150) {
            // no new bytes for 150ms after we've started getting data -> assume done
            break;
        }
    }
    Serial.println(resp);
    return resp;
}


void drawReceivingScreen() {
  tft.fillScreen(UI_BG);
  uiUseTitleFont();
  tft.setTextColor(UI_TEXT);
  tft.setCursor(20, 140);
  tft.print("Checking for");
  tft.setCursor(20, 168);
  tft.print("messages");
  tft.setFont(NULL);
  tft.drawFastHLine(20, 184, 200, UI_ACCENT);
}

void receive() {

   // modemWake();

    drawReceivingScreen();

    // FIX: removed the "AT+CMGF=1" resend here — text mode is a persistent
    // modem setting already established once in modemConfigure() at boot;
    // nothing in this codebase switches it back to PDU mode, so resending
    // it on every Refresh tap was a redundant round trip.
    SerialSARA.println("AT+CMGL=\"ALL\"");
    

    //Read entire response first 
    String fullResponse = readModemResponse(3000);

    // Print for debug 
    Serial.println("=== MODEM RESPONSE ===");
    Serial.println(fullResponse);
    Serial.println("======================");

    //  Now parse line by line
    bool anyReceived = false;
    int parseIdx = 0;

    while (parseIdx < (int)fullResponse.length()) {
        // find next line
        int lineEnd = fullResponse.indexOf('\n', parseIdx);
        if (lineEnd == -1) break;

        String line = fullResponse.substring(parseIdx, lineEnd);
        line.replace("\r", "");  // strip carriage return
        parseIdx = lineEnd + 1;

        if (!line.startsWith("+CMGL:")) continue;

        anyReceived = true;

        // extract index
        int colonIdx = line.indexOf(':');
        int commaIdx = line.indexOf(',');
        int msgIndex = line.substring(colonIdx + 2, commaIdx).toInt();
        Serial.println(msgIndex);

        // extract sender
        int q1 = line.indexOf('"', commaIdx);
        int q2 = line.indexOf('"', q1 + 1);
        int q3 = line.indexOf('"', q2 + 1);
        int q4 = line.indexOf('"', q3 + 1);
        String sender = line.substring(q3 + 1, q4);

        // extract timestamp
        int lastQ2 = line.lastIndexOf('"');
        int lastQ1 = line.lastIndexOf('"', lastQ2 - 1);
        String timestamp = line.substring(lastQ1 + 1, lastQ2);

        // read body — next non-empty line
        String body = "";
        while (parseIdx < (int)fullResponse.length()) {
            int nextEnd = fullResponse.indexOf('\n', parseIdx);
            if (nextEnd == -1) nextEnd = fullResponse.length();
            String nextLine = fullResponse.substring(parseIdx, nextEnd);
            nextLine.replace("\r", "");
            parseIdx = nextEnd + 1;
            if (nextLine.length() > 0 && !nextLine.startsWith("+CMGL") && !nextLine.startsWith("OK")) {
                body = nextLine;
                break;
            }
        }

        Serial.print("From: "); Serial.println(sender);
        Serial.print("Time: "); Serial.println(timestamp);
        Serial.print("Body: "); Serial.println(body);

        char normalizedNumber[MAX_PHONE_LEN];
        char timestampBuf[MAX_TIMESTAMP_LEN];
        char bodyBuf[200];



        normalizePhoneNumber(sender.c_str(), normalizedNumber, MAX_PHONE_LEN);
        copyBounded(timestampBuf, timestamp.c_str(), MAX_TIMESTAMP_LEN);
        copyBounded(bodyBuf, body.c_str(), 200);

        char formatted[20];
        formatTimestamp(timestampBuf, formatted, sizeof(formatted));
        Serial.println(formatted);


        pushMessage(normalizedNumber, bodyBuf, IN, formatted);


        // Flush out the read message 
        char delCmd[20];
        snprintf(delCmd, sizeof(delCmd), "AT+CMGD=%d", msgIndex);
        SerialSARA.println(delCmd);
        delay(300);
        while (SerialSARA.available()) SerialSARA.read();
    }
    // Keep goinh until we have looped through the entire message 
    // (modem prints out all of the unread messages)

    if (!anyReceived) {
        Serial.println("No new messages");
        // FIX: previously printed this then returned immediately, so the
        // caller's screen transition overwrote it before it was ever
        // visible (0ms on screen). Now it actually gets seen.
        tft.fillRect(0, 160, tft.width(), 40, UI_BG);
        tft.setTextSize(1);
        tft.setTextColor(UI_TEXT_DIM);
        tft.setCursor(20, 172);
        tft.print("No new messages");
        delay(700);
    }
    //modemSleep();
}


void text(const char* remoteNum, const char* message) {

  // modemWake();

  // ── Sending loading screen ────────────────────────────────
  tft.fillScreen(UI_BG);

  int cidx = findContactName(remoteNum);
  const char* toLabel = (cidx != -1) ? getContactName(cidx) : remoteNum;

  uiUseTitleFont();
  tft.setTextColor(UI_TEXT_DIM);
  tft.setCursor(20, 110);
  tft.print("SENDING TO");

  tft.setTextColor(UI_TEXT);
  tft.setCursor(20, 140);
  tft.print(toLabel);
  tft.setFont(NULL);

  tft.drawFastHLine(20, 156, 200, UI_ACCENT);

  tft.setTextSize(1);
  tft.setTextColor(UI_ACCENT);
  tft.setCursor(20, 172);
  tft.print("Sending...");

  // Signal looks good, attempt send
  sms.beginSMS(remoteNum);
  sms.print(message);
  int result = sms.endSMS();

  // swap the status line for the result
  tft.fillRect(0, 166, tft.width(), 20, UI_BG);
  tft.setCursor(20, 172);

  if (result == 1) {
    char sentTimestamp[MAX_TIMESTAMP_LEN];
    if (!getCurrentTimestamp(sentTimestamp, sizeof(sentTimestamp))) { // If our timestamp function messes up
      copyBounded(sentTimestamp, "Unknown", sizeof(sentTimestamp));
    }
    pushMessage(remoteNum, message, OUT, sentTimestamp);
    tft.setTextColor(UI_ACCENT);
    tft.print("Sent");
  } else {
    tft.setTextColor(UI_DANGER);
    tft.print("Failed");
  }
  // modemSleep();
  delay(1000);
}

// ── UI state machine ──────────────────────────────────────────────

enum UiState { UI_MENU,
               UI_MESSAGES,
               UI_REFRESH,
               UI_COMPOSE,
               UI_CONVO,
               UI_CONTACTS,
               UI_ADD_CONTACT,
               UI_DEBUG };


UiState currentState = UI_MENU;

// ── Splash ────────────────────────────────────────────────────────

void drawSplashScreen() {
  tft.setRotation(ROTATION);
  tft.fillScreen(ILI9341_WHITE);  // physically renders BLACK — see splash_logo.h
  int16_t x = (240 - SPLASH_LOGO_W) / 2;
  int16_t y = (320 - SPLASH_LOGO_H) / 2;
  tft.drawBitmap(x, y, splash_logo_bits, SPLASH_LOGO_W, SPLASH_LOGO_H, ILI9341_BLACK);
}

// ── Setup ─────────────────────────────────────────────────────────

void setup() {
  Serial.begin(115200);
  while (!Serial && millis() < 3000);


  // ── Pin setup ─────────────────────────────────────────
  pinMode(TFT_CS, OUTPUT);
  digitalWrite(TFT_CS, HIGH); // Stop listening
  pinMode(SD_CS, OUTPUT);
  digitalWrite(SD_CS, HIGH);    // Stop listening

  // ── Touch controller reset ────────────────────────────
  pinMode(CTP_RST, OUTPUT);
  digitalWrite(CTP_RST, LOW);  delay(10);
  digitalWrite(CTP_RST, HIGH); delay(50);
  Wire.begin();
  // NOTE: CTP_INT-gated polling was tried and reverted (see loop()) — it
  // caused ghost repeat touches, likely because this FT6336U runs in
  // trigger mode rather than level/polling mode. CTP_INT stays unused.

  // ── TFT FIRST ─────────────────────────────────────────
  // Turn screen on
  pinMode(LED_SCREEN, OUTPUT);  
  digitalWrite(LED_SCREEN, HIGH);
  lastTouchTime = millis();

  tft.begin();  // -> Internal start listening TFT_CS = LOW and pulls it high once done.
  tft.invertDisplay(true);
  uiInitColors();  // populate the shared dark-minimalist palette (UI.h) before any screen draws
  drawSplashScreen();  // stays up through SD + cellular init, until the menu draws

// ── SD AFTER TFT ──────────────────────────────────────
  bool sdReady = false;
  int retries = 3;
  while (retries--) {
    if (SD.begin(SD_CS)) {
      Serial.println("SD ready");
      sdReady = true;
      loadContactsFromSD();
      loadMessagesFromSD();
      break;
    }
    Serial.println("SD init failed, retrying...");
    delay(500);
  }

  if (!sdReady) {
    Serial.println("SD init failed permanently — continuing without SD.");
    tft.fillScreen(ILI9341_BLACK);
    tft.setCursor(0, 0);
    tft.println("SD card error.");
    tft.println("Running without storage.");
    delay(1500);
  } else {
    if (!SD.exists("contacts.csv")) {
      File f = SD.open("contacts.csv", FILE_WRITE);
      if (f) { f.close(); }
      Serial.println("Contacts file created");
    }

    if (!SD.exists("msg")) {
      SD.mkdir("msg");
      Serial.println("msg directory created");
    }
  }

  // If it doesent exist currently the FW just conitnues on using the pre - NV MEM FW, eventually need to make more iOS like. 
  

  // ── Cellular ──────────────────────────────────────────
  bool connected = false;
  while (!connected) {
    if (nbAccess.begin("") == NB_READY) {
      connected = true;
    } else {
      tft.fillScreen(ILI9341_BLACK);
      tft.println("Not connected");
      Serial.println("Not connected");
      delay(1000);
    }
  }

  modemConfigure();

   // modemSleep();   // registered — now let it default to power-saving from here on

  pinMode(SLEEP_PIN, INPUT_PULLDOWN);  // Bring the pin down to 0 from ambiguos mode 

}

void modemConfigure() {
    SerialSARA.println("AT+UPSV=4");        // UART always on, modem can idle
    readModemResponse(500);
    
    /*
    SerialSARA.println("AT+CPSMS=0");       // disable PSM
    readModemResponse(500);
    
    SerialSARA.println("AT+CEDRXS=1,4,\"0101\"");  // enable eDRX 81 sec
    readModemResponse(500);
    
    SerialSARA.println("AT+CMGF=1");        // text mode
    readModemResponse(500);
    
    SerialSARA.println("AT+CPMS=\"SM\",\"SM\",\"SM\"");  // SIM memory
    readModemResponse(500);
    
    SerialSARA.println("AT+CMGD=1,1");      // clear stale read messages
    readModemResponse(500);

    // FIX: removed "AT+CEDRXS?" — it only echoes back the request you just
    // sent above, which you already know. AT+CEDRXRDP below is the one
    // that's actually useful, since it reports what the network granted.
    SerialSARA.println("AT+CEDRXRDP");
    readModemResponse(500);
    */


    Serial.println("Modem configured");
}


// ── Loop ──────────────────────────────────────────────────────────

void loop() {

  unsigned long now = millis();

  // Serial.print("SLEEP_PIN: ");
  // Serial.println(digitalRead(SLEEP_PIN));
  
  // Pin 1 sleep control 

    static bool wasSleeping = false;
    bool sleeping = digitalRead(SLEEP_PIN) == HIGH;

    if (sleeping && !wasSleeping) {
        enterSleep();
        wasSleeping = true;
    } else if (!sleeping && wasSleeping) {
        exitAndReset();
        wasSleeping = false;
    }

    if (sleeping) return;

  static Button msgBtn, compBtn, refreshBtn, contactsBtn, backBtn, debugBtn;
  static bool isDrawnConvo = false;

  if (!menuDrawn) {
    tft.fillScreen(UI_BG);
    tft.setRotation(ROTATION);

    // Flat minimalist rows — same tap-target geometry as before (full width,
    // stacked), just skinned as hairline-divided list rows instead of solid
    // filled buttons. color=UI_BG makes each row's own fill blend into the
    // background; Button::render() still draws the label for us.
    const int rowH   = 52;
    const int rowGap = 4;
    const int rowY0  = 70;

    // FIX: was `if(unreadMessage)` — missing the call, so this always
    // evaluated a function pointer (always truthy) instead of the actual
    // unread state. See messages.cpp for the unreadMessage() fix too.
    bool unread = unreadMessage();

    msgBtn.initButton(0, rowY0,                     240, rowH, "Messages", UI_BG);
    compBtn.initButton(0, rowY0 + (rowH+rowGap)*1,   240, rowH, "Compose",  UI_BG);
    refreshBtn.initButton(0, rowY0 + (rowH+rowGap)*2, 240, rowH, "Refresh", UI_BG);
    contactsBtn.initButton(0, rowY0 + (rowH+rowGap)*3, 240, rowH, "Contacts", UI_BG);
    //  debugBtn.initButton(...)

    Button* menuRows[4] = { &msgBtn, &compBtn, &refreshBtn, &contactsBtn };
    for (int i = 0; i < 4; i++) {
      Button* b = menuRows[i];
      tft.setFont(&FreeSans9pt7b);
      tft.setTextSize(1);
      tft.setTextColor(UI_TEXT_DIM);
      tft.setCursor(b->x + b->width - 16, b->y + b->height/2 + 4);
      tft.print(">");
      tft.setFont(NULL);
      tft.drawFastHLine(16, b->y + b->height, b->width - 32, UI_BORDER);
    }

    // Unread indicator — a small dot ahead of the chevron rather than
    // recoloring the whole row.
    if (unread) {
      tft.fillCircle(msgBtn.x + msgBtn.width - 28, msgBtn.y + msgBtn.height/2, 4, UI_DANGER);
    }

    // Status row — clock / signal / battery — sits above the nav rows,
    // separated by its own hairline.
    updateClock();
    updateCSQ();
    updateBattery();
    tft.drawFastHLine(0, 26, 240, UI_BORDER);

    menuDrawn = true;
  }

  // If there is a notification on any of the chats, add a blue iOS style circle over the messages rectangle!



  // Check signal first


  bool touched = false;
  ScreenPoint sp;

  // REVERTED: gating this read on CTP_INT's level caused repeated/ghost
  // key presses (a single tap registering as "aaa"). This FT6336U appears
  // to run in trigger mode — CTP_INT pulses low once per new report rather
  // than staying asserted for the whole touch — so gating on level made
  // `touched` flicker false mid-touch, and the debounce logic below read
  // that as release-then-press repeatedly. Back to unconditional polling,
  // which is the version that was actually reliable.
  if (ctpRead(sp)) {
    touched = true;
  }


  bool justPressed = touched && !wasTouched;
  wasTouched = touched;

/*
    // ── Screen sleep/wake handling ─────────────────────────────
    if (touched) {
      lastTouchTime = now;
    }

    if (!screenAsleep && (now - lastTouchTime >= SCREEN_SLEEP_TIMEOUT)) {
      // screenSleep(); Using analog switch for this currently 
    }

    if (screenAsleep) {
      if (touched) {
        // screenWake(); -> for now relying on analog switch to determine screen/touch sleep mode. 
      }
      return;   // swallow this loop's touch — don't let it fall through to the state machine
    }
*/ 

  switch (currentState) {

    case UI_MENU:
      {



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
          // receive() draws its own loading/result screens now, so nothing
          // needs to be drawn here first.
          receive();
          while (ctpRead(sp)) delay(10);
          // Land on Recents (not the menu) — moveThreadToTop() already
          // bubbles whatever thread just got a new message to the top, so
          // this is effectively "here's what you just got," with the same
          // back-to-menu and tap-to-open-conversation behavior Recents
          // already has.
          recentMessagesReset();
          currentState = UI_MESSAGES;
          return;
        }
        

        if (justPressed && contactsBtn.isClicked(sp)) {
          // tft.fillScreen(ILI9341_ORANGE);
          tft.setCursor(0, 0);
          // tft.println("Other button touched");
          currentState = UI_CONTACTS;
          // menuDrawn = false;
          return;
        }
        if (justPressed && debugBtn.isClicked(sp)) {
          currentState = UI_DEBUG;
          return;
        }
        break;
      }

    case UI_MESSAGES:
      {
        // recentMessagesScreen(sp, false);
        int picked = recentMessagesScreen(sp, justPressed);
        if (justPressed && msgBackBtnPressed(sp)) {
          recentMessagesReset();
          currentState = UI_MENU;
          menuDrawn = false;
          wasTouched = true;
          return;
        }
        if (picked != -1) {
          displayConvo = picked;
          conversationReset();
          currentState = UI_CONVO;
          wasTouched = true;
          return;
        }
        break;
      }

    case UI_CONVO:
      {
        drawConversationToTFT(displayConvo, sp, justPressed);

        if (justPressed && convoBackBtnPressed(sp)) {
          conversationReset();
          recentMessagesReset();
          currentState = UI_MESSAGES;
          wasTouched = true;
          return;
        }
        if (justPressed && convoReplyBtnPressed(sp)) {
          const char* phone = getConversationPhone(displayConvo);
          if (phone != nullptr) {
            strncpy(recipientNumber, phone, MAX_PHONE_LEN - 1);  // already normalized
            recipientNumber[MAX_PHONE_LEN - 1] = '\0';
            numberAquired = true;
            conversationReset();
            recentMessagesReset();
            keyboardReset();

            int cidx = findContactName(recipientNumber);
            keyboardSwitchToMessageField(cidx != -1 ? getContactName(cidx) : recipientNumber, KB_COMPOSE);

            currentState = UI_COMPOSE;
            return;
          }
        }
        break;
      }

    case UI_COMPOSE:
      {
        if (!numberAquired) {
          if (justPressed && keyboardBackPressed(sp)) {
            currentState = UI_MENU;
            menuDrawn = false;
            return;
          }
          if (justPressed && msgBtnPressed(sp)) {
            const char* kb = keyboardGetText();
            normalizePhoneNumber(kb, recipientNumber, MAX_PHONE_LEN); // So no double of same numbers
            // strncpy(recipientNumber, kb, MAX_PHONE_LEN - 1); THis was overwriting the normalized e.164 formatted phone # 
            recipientNumber[MAX_PHONE_LEN - 1] = '\0';
            numberAquired = true;
            //keyboardClearText();

            // Check if it matches a contact
            int idx = findContactName(recipientNumber); // Looks by phone # and returns idx

            if(idx == -1){
            keyboardSwitchToMessageField(recipientNumber, KB_COMPOSE);
            }else{
            keyboardSwitchToMessageField(getContactName(idx), KB_COMPOSE);
            }

            Serial.println("Phone # acquired");
            wasTouched = true;
          } else if (keyboardTick(sp, justPressed, KB_COMPOSE)) {
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
          } else if (keyboardTick(sp, justPressed, KB_COMPOSE)) {
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

    case UI_CONTACTS:
      {
        int picked = contactsScreen(sp, justPressed);
        if (picked == -3) {
          contactsScreenReset();
          keyboardReset();
          currentState = UI_ADD_CONTACT;
          return;
        }
        if (picked == -2) {
          contactsScreenReset();

          currentState = UI_MENU;
          menuDrawn = false;
          return;
        }
        if (picked != -1) {
          const char* phone = getContactPhone(picked);
        if (phone != nullptr) {
            strncpy(recipientNumber, phone, MAX_PHONE_LEN - 1);  // already normalized
            recipientNumber[MAX_PHONE_LEN - 1] = '\0';
            numberAquired = true;
            contactsScreenReset();
            keyboardReset();
            
            const char* name = getContactName(picked);
            keyboardSwitchToMessageField(name != nullptr ? name : recipientNumber, KB_COMPOSE);
            
            currentState = UI_COMPOSE;
            return;
        }
        }
        break;
      }

    case UI_ADD_CONTACT:
      {



        if (justPressed && keyboardBackPressed(sp)) {
          contactsScreenReset();
          keyboardReset();
          numberAquired = false;
          currentState = UI_CONTACTS;
          return;
        }

        if (!numberAquired) {
          // step 1 — PHONE NUMBER (numpad, no changes needed since keyboardReset starts in numpad)
          if (justPressed && nameBtnPressed(sp)) {
            const char* kb = keyboardGetText();
            normalizePhoneNumber(kb, newContactPhone, MAX_PHONE_LEN);
            newContactPhone[MAX_PHONE_LEN - 1] = '\0';
            numberAquired = true;
            keyboardSwitchToMessageField(newContactPhone, KB_ADD_CONTACT);  // freeze phone in # field
            Serial.println("Phone acquired");
            wasTouched = true;
          } else if (keyboardTick(sp, justPressed, KB_ADD_CONTACT)) {
            Serial.println("Error: Send with no phone");
          }
        } else {
          // step 2 — NAME (alpha keyboard since keyboardSwitchToMessageField sets alphaMode=true)
          if (justPressed && numberBtnPressed(sp)) {
            numberAquired = false;
            keyboardReset();
          }
          if (justPressed && keyboardBackPressed(sp)) {
            currentState = UI_MENU;
            menuDrawn = false;
            return;
          }

          else if (keyboardTick(sp, justPressed, KB_ADD_CONTACT)) {
            const char* kb2 = keyboardGetText();
            
            strncpy(newContactName, kb2, sizeof(newContactName) - 1);
            newContactName[sizeof(newContactName) - 1] = '\0';
            addContactFromUI(newContactName, newContactPhone);
            keyboardReset();
            contactsScreenReset();
            numberAquired = false;
            currentState = UI_CONTACTS;
            return;
          }
        }


        break;
      }

      /*
case UI_DEBUG: {
  if (justPressed && keyboardBackPressed(sp)) {
    keyboardReset();
    currentState = UI_MENU;
    menuDrawn = false;
    return;
  }

  if (keyboardTick(sp, justPressed, KB_DEBUG)) {
    const char* cmd = keyboardGetText();

    // send to modem
    SerialSARA.println(cmd);
    delay(300);

    // read response
    char response[160];
    int i = 0;
    while (SerialSARA.available() && i < 159) {
      char c = (char)SerialSARA.read();
      if (c != '\r') response[i++] = c;  // strip carriage returns
    }
    response[i] = '\0';

    // display cmd + response, wipe previous
    debugPrint(cmd, response);

    // clear input field only
    keyboardReset();
  }
  break;
}
*/




    default: break;
  }
}





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
  tft.sendCommand(0x10);       // SLPIN — panel + driver IC to sleep
  delay(5);                    // ILI9341 needs >=5ms before another command
  digitalWrite(LED_SCREEN, LOW);
  digitalWrite(TFT_CS, HIGH);  // deasserted, matches SD_CS below
  screenAsleep = true;
}

void screenWake(){
  if (!screenAsleep) return;
  tft.begin();                 // full cold re-init — registers/GRAM are gone
  tft.setRotation(ROTATION);
  tft.invertDisplay(true);
  screenAsleep = false;
  lastTouchTime = millis();
  // backlight is enabled by exitAndReset() only after real content is
  // redrawn, so the panel never flashes a frame of garbage GRAM
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

// Forces whatever's on currentState to fully repaint next pass, since a
// real Vcc cut wipes GRAM. Uses each screen's existing "*Drawn = false"
// reset function rather than inventing a new redraw path. keyboardReset()
// is deliberately avoided for the compose/add-contact states — it would
// also wipe whatever the user had typed, which we don't want after a
// transient screen power cycle.
void redrawCurrentScreen() {
    switch (currentState) {
        case UI_MENU:
            menuDrawn = false;
            break;
        case UI_MESSAGES:
            recentMessagesReset();
            break;
        case UI_CONVO:
            conversationReset();
            break;
        case UI_CONTACTS:
            contactsScreenReset();
            break;
        case UI_COMPOSE:
        case UI_ADD_CONTACT:
            keyboardForceRedraw();
            break;
        default:
            break;
    }
}

volatile bool wakeRequested = false;

void wakeISR() {
    wakeRequested = true;
}

void enterSleep() {
   SerialSARA.println("AT+CEDRXS=1,4,\"0101\"");
   readModemResponse(500);
   // FIX: removed "AT+CEDRXSP=?" — not a real u-blox command (only +CEDRXS,
   // +CEDRXRDP, and the +CEDRXP URC exist), so this always returned ERROR
   // while still blocking here for up to 500ms on every single sleep entry.

    // Dignified shutdown of the screen board before its Vcc is (eventually)
    // cut — order matches the reverse of the boot sequence in setup().
    screenSleep();
    ctpSleep();
    sdSleep();

    wakeRequested = false;
    LowPower.attachInterruptWakeup(SLEEP_PIN, wakeISR, FALLING);
    LowPower.deepSleep();

    if (wakeRequested) exitAndReset();
}



void exitAndReset() {
   SerialSARA.println("AT+CEDRXS=0");

    // Cold-boot the screen board back up, same relative order as setup():
    // touch (I2C) → panel (SPI) → SD (SPI). Backlight stays off through all
    // of it — screenWake() re-inits the panel but doesn't touch LED_SCREEN.
    ctpReset();
    screenWake();
    sdWake();

   //  redrawCurrentScreen();  // paint real content before the backlight comes on
    digitalWrite(LED_SCREEN, HIGH);

    lastTouchTime   = millis();
    lastCSQUpdate   = millis();
    lastClockUpdate = millis();

    readModemResponse(2000);
}


/*void exitAndReset() {
    // restore VCC first
    digitalWrite(MOSFET_PIN, LOW);
    delay(50);
    
    // restore SPI
    pinMode(MOSI, OUTPUT);
    pinMode(MISO, INPUT);
    pinMode(SCK, OUTPUT);
    pinMode(TFT_CS, OUTPUT);
    pinMode(SD_CS, OUTPUT);
    pinMode(TFT_DC, OUTPUT);
    digitalWrite(TFT_CS, HIGH);
    digitalWrite(SD_CS, HIGH);
    SPI.begin();
    
    // restore I2C
    Wire.begin();                    // reinitializes SERCOM
                                     // takes back SDA/SCL pins
    
    // reinit peripherals
    tft.begin();
    tft.setRotation(ROTATION);
    tft.invertDisplay(true);
    SD.begin(SD_CS);
    ctpReset();
    delay(100);
    screenWake();
    
    menuDrawn = false;
    lastTouchTime = millis();
    lastCSQUpdate = millis();
    lastClockUpdate = millis();
}*/