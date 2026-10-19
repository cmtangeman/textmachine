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
#include "math.h"

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
#include "nav_icons.h"
#include "modes.h"  // sleep/wake API — also brings in TFT_CS/CTP_RST/SD_CS/ROTATION/SCREEN_PWR_PIN/SLEEP_PIN/LED_SCREEN below
#include "settings.h"

// ── Pin definitions ───────────────────────────────────────────────
#define TFT_DC 7
#define TFT_RST -1

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

// SD_CS / ROTATION / SCREEN_PWR_PIN now come from modes.h

// ── Peripherals ───────────────────────────────────────────────────
Adafruit_ILI9341 tft = Adafruit_ILI9341(TFT_CS, TFT_DC, TFT_RST);


// ── App state ─────────────────────────────────────────────────────
bool menuDrawn = false;
bool numberAquired = false;
int displayConvo = 0;
// Phone number of whichever conversation was opened from Recents — set once,
// the first time the user taps into a thread (see the UI_MESSAGES case
// below), and used by convoRxBtnPressed's handler to re-find that same
// conversation's index after receive() potentially reorders threads.
// A fixed buffer, not a bare pointer: threads[] slots get overwritten in
// place by moveThreadToTop(), so a raw `const char*` aliasing straight into
// threads[idx].phoneNumber would silently start pointing at a *different*
// conversation's data the moment any thread reorders — this holds a copy.
char convoNumber[MAX_PHONE_LEN] = "";
bool replyFromConvo = false;  // true while composing via the conversation screen's reply bar — routes back to UI_CONVO instead of UI_MENU on send/back



static char recipientNumber[MAX_PHONE_LEN];
static char msgBody[MAX_BODY_LEN];


unsigned long lastCSQUpdate = 0;
unsigned long lastClockUpdate = 0;

#define CSQ_INTERVAL 300000    // 5 minutes
#define CLOCK_INTERVAL 100000  // 1 min

NB nbAccess;
NB_SMS sms;
//NBModem modem;


// LOW POWER MODE STUFF — enterSleep()/exitAndReset()/etc. now live in modes.cpp,
// which includes ArduinoLowPower.h itself.

#define PWR_ON_PULSE_MS 250 // stay well inside the 150-3200ms window, away from the power-off overlap zone

//FOR TOUCH SLEEP TIME.
// SLEEP_PIN now comes from modes.h

#define FT_REG_POWER_MODE  0xA5
#define FT_POWER_ACTIVE    0x00
#define FT_POWER_MONITOR   0x01  // low power, still detects touch
#define FT_POWER_HIBERNATE 0x03  // full sleep

//FOR SCREEN SLEEP TIME.
// LED_SCREEN now comes from modes.h

#define SCREEN_SLEEP_TIMEOUT 30000UL  // 30s

unsigned long lastTouchTime = 0;
bool screenAsleep = false;

// Hoisted out of loop() so exitAndReset() can clear stale debounce state
// left over from the instant sleep was entered.
bool wasTouched = false;

int brightness = 250;

#define CTP_WAKE_TIMEOUT_MS 300

// Some forward declrations
void modemConfigure();

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
  // FT6336U always reports in native (rotation 0) panel space, regardless
  // of tft.setRotation() — landscape (compose screen's rotate button,
  // keyboard.cpp) needs this remap decided at RUNTIME, since it's the one
  // place this app's rotation actually changes while running; the #if
  // block below is unchanged and still handles the fixed compile-time
  // ROTATION for everything else.
  if (keyboardIsLandscape()) {
    uint16_t tmp = x;
    x = y;
    y = 240 - 1 - tmp;
  } else {
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
  // ROTATION == 0 (== 4 here) needs no remap
  }

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


// Plain text receiving screen — same layout language as text()'s "SENDING
// TO" screen: title, hairline, status line that gets updated in place.
void drawReceivingScreen() {
  tft.fillScreen(UI_BG);

  uiUseTitleFont();
  tft.setTextColor(UI_TEXT_DIM);
  tft.setCursor(20, 110);
  tft.print("CHECKING FOR");

  tft.setTextColor(UI_TEXT);
  tft.setCursor(20, 140);
  tft.print("Messages");
  tft.setFont(NULL);

  tft.drawFastHLine(20, 156, 200, UI_ACCENT);

  tft.setTextSize(1);
  tft.setTextColor(UI_ACCENT);
  tft.setCursor(20, 172);
  tft.print("Checking...");
}

// Returns true if at least one new message was pulled down and stored.
bool receive() {

    Serial.println("=== RECEIVE CALLED ===");
    
    // check modem responsive before doing anything
    while (SerialSARA.available()) {
        char c = (char)SerialSARA.read();
        Serial.print("STALE: ");
        Serial.println(c);
    }
    
    SerialSARA.println("AT");
    String ping = readModemResponse(1000);
    Serial.print("Modem ping: ");
    Serial.println(ping);
    
    // if modem not responding → bail immediately
    if (ping.indexOf("OK") == -1) {
        Serial.println("Modem not responsive — aborting receive");
        return false;
    }

    drawReceivingScreen();


    SerialSARA.println("AT+CMGL=\"ALL\"");


    String fullResponse = "";
    unsigned long rxStart     = millis();
    unsigned long rxLastByte  = millis();
    // unsigned long rxLastFrame = 0;  // was the radar sweep's frame clock
    // int sweepDeg = 0;               // was the radar sweep's rotation angle
    const unsigned long rxTimeoutMs = 8000;

    while (millis() - rxStart < rxTimeoutMs) {
        if (SerialSARA.available()) {
            char c = (char)SerialSARA.read();
            fullResponse += c;
            rxLastByte = millis();
        } else if (fullResponse.length() > 0 && millis() - rxLastByte > 500) {
            break;
        }

        // if (millis() - rxLastFrame >= 90) {
        //     drawRadarSweep(sweepDeg);
        //     sweepDeg = (sweepDeg + 18) % 360;
        //     rxLastFrame = millis();
        // }
    }

    // Print for debug
    Serial.println("=== MODEM RESPONSE ===");
    Serial.println(fullResponse);
    Serial.println("======================");

    // One "now" fetch for this whole batch of messages — formatTimestamp()
    // needs it to tell how many days old each message is (weekday+time vs.
    // just the date), and there's no reason to re-query the modem's clock
    // separately per message in the loop below.
    char nowRaw[32];
    bool haveNow = getCurrentRawTimestamp(nowRaw, sizeof(nowRaw));

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

        // Radar blip-per-message reveal disabled along with the rest of the
        // radar animation — see the commented-out block above.
        // radarAddBlip();
        // drawRadarSweep(sweepDeg);
        // sweepDeg = (sweepDeg + 18) % 360;
        // delay(80);

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
        formatTimestamp(timestampBuf, haveNow ? nowRaw : timestampBuf, formatted, sizeof(formatted));
        Serial.println(formatted);

        pushMessage(normalizedNumber, bodyBuf, IN, formatted, timestampBuf);

        // Flush out the read message
        char delCmd[20];
        snprintf(delCmd, sizeof(delCmd), "AT+CMGD=%d", msgIndex);
        SerialSARA.println(delCmd);
        delay(300);
        while (SerialSARA.available()) SerialSARA.read();
    }
    // Keep goinh until we have looped through the entire message
    // (modem prints out all of the unread messages)

    // swap the status line for the result, same pattern as text()'s Sent/Failed swap
    tft.fillRect(0, 166, tft.width(), 20, UI_BG);
    tft.setCursor(20, 172);
    if (anyReceived) {
        tft.setTextColor(UI_ACCENT);
        tft.print("Received");
    } else {
        Serial.println("No new messages");
        tft.setTextColor(UI_TEXT_DIM);
        tft.print("No new messages");
    }
    delay(700);

    //modemSleep();
    return anyReceived;
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
    // One modem clock query, reused both for the display string (via
    // formatTimestamp(), same as getCurrentTimestamp() does internally)
    // and for the raw copy pushMessage() packs for the double-tap
    // full-timestamp reveal — no need to query twice.
    char sentTimestampRaw[32];
    char sentTimestamp[MAX_TIMESTAMP_LEN];
    bool haveSentRaw = getCurrentRawTimestamp(sentTimestampRaw, sizeof(sentTimestampRaw));
    if (haveSentRaw) {
      formatTimestamp(sentTimestampRaw, sentTimestampRaw, sentTimestamp, sizeof(sentTimestamp));
    } else {
      copyBounded(sentTimestamp, "Unknown", sizeof(sentTimestamp)); // If our timestamp function messes up
    }
    pushMessage(remoteNum, message, OUT, sentTimestamp, haveSentRaw ? sentTimestampRaw : nullptr);
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
               UI_DEBUG,
               UI_SETTINGS };


UiState currentState = UI_MENU;

// ── Splash ────────────────────────────────────────────────────────

void drawSplashScreen() {
  tft.setRotation(ROTATION);
  // FIX: this used to fillScreen(WHITE) + draw the logo bits in BLACK,
  // relying on tft.invertDisplay(true) (now removed, see modes.cpp's
  // screenWake()) to physically render it as white ink on black. With
  // inversion gone, these need to be the literal colors we want on screen —
  // swapped back to black background / white ink to match the rest of the
  // app's dark theme instead of flashing white.
  tft.fillScreen(ILI9341_BLACK);
  int16_t x = (240 - SPLASH_LOGO_W) / 2;
  int16_t y = (320 - SPLASH_LOGO_H) / 2;
  tft.drawBitmap(x, y, splash_logo_bits, SPLASH_LOGO_W, SPLASH_LOGO_H, ILI9341_WHITE);
}

// ── Setup ─────────────────────────────────────────────────────────

void setup() {
  Serial.begin(115200);
  while (!Serial && millis() < 3000);

  pinMode(SCREEN_PWR_PIN, OUTPUT);
  digitalWrite(SCREEN_PWR_PIN, LOW);  // screen ON at boot

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
  digitalWrite(LED_SCREEN, LOW); // V_LED_SCREEN = Vcc

  analogWrite(LED_SCREEN, brightness);


  lastTouchTime = millis();

  tft.begin();  // -> Internal start listening TFT_CS = LOW and pulls it high once done.
  // tft.invertDisplay(true);
  uiInitColors();  // populate the shared dark-minimalist palette (UI.h) before any screen draws
  drawSplashScreen();  // stays up through SD + cellular init, until the menu draws


  // BRIGHTNESS 

  

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
  void modemConfigure();

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
    SerialSARA.println("AT+UPSV=0");        // UART always on, modem can idle
    Serial.println(" -> AT+UPSV = 0 ");
    readModemResponse(500);

    SerialSARA.println("AT+CPMS=\"ME\",\"ME\",\"ME\"");// Using modem memory
    readModemResponse(500);

    Serial.println("Modem configured");
}



static void drawNavTile(Button& btn, int x, int y, int w, int h,
                         const uint8_t* iconBits, int iconW, int iconH,
                         const char* label, bool showBadge) {
  btn.x = x; btn.y = y; btn.width = w; btn.height = h;
  btn.text = label; btn.color = UI_SURFACE;

  tft.fillRoundRect(x, y, w, h, 12, UI_SURFACE);

  int iconX = x + (w - iconW) / 2;
  int iconY = y + (h - iconH) / 2;
  tft.drawBitmap(iconX, iconY, iconBits, iconW, iconH, UI_TEXT);

  if (showBadge) {
    tft.fillCircle(x + w - 10, y + 10, 5, UI_DANGER);
  }
}



void loop() {

  unsigned long now = millis();



    static bool wasSleeping = false;
    // This is not an interrupt it is read from normal sequential polling of the sleep pin. 
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
  static Button menuSwipeBtn;  // bottom pill — opens UI_SETTINGS
  static bool isDrawnConvo = false;

  if (!menuDrawn) {
    tft.fillScreen(UI_BG);
    tft.setRotation(ROTATION);

    // 2x2 app-style icon grid — Messages/Compose on top, Refresh/Contacts
    // below. Top row keeps its original 124px card height; the bottom row
    // (Refresh/Contacts) is shorter, freeing room for the swipe-indicator
    // pill beneath it that opens Settings.
    const int outerMarginX = 14;
    const int gapX         = 14;
    const int gapY         = 14;
    const int tileW        = (240 - 2 * outerMarginX - gapX) / 2;
    const int tileHTop     = 124;
    const int tileHBottom  = 102;
    const int gridTop      = 34;

    const int col0X = outerMarginX;
    const int col1X = outerMarginX + tileW + gapX;
    const int row0Y = gridTop;
    const int row1Y = gridTop + tileHTop + gapY;
    const int row1Bottom = row1Y + tileHBottom;

    // FIX: was `if(unreadMessage)` — missing the call, so this always
    // evaluated a function pointer (always truthy) instead of the actual
    // unread state. See messages.cpp for the unreadMessage() fix too.
    bool unread = unreadMessage();

    drawNavTile(msgBtn,      col0X, row0Y, tileW, tileHTop,    RECENTS_ICON_bits,  RECENTS_ICON_W,  RECENTS_ICON_H,  "Messages", unread);
    drawNavTile(compBtn,     col1X, row0Y, tileW, tileHTop,    COMPOSE_ICON_bits,  COMPOSE_ICON_W,  COMPOSE_ICON_H,  "Compose",  false);
    drawNavTile(refreshBtn,  col0X, row1Y, tileW, tileHBottom, RX_ICON_bits,       RX_ICON_W,       RX_ICON_H,       "Refresh",  false);
    drawNavTile(contactsBtn, col1X, row1Y, tileW, tileHBottom, CONTACTS_ICON_bits, CONTACTS_ICON_W, CONTACTS_ICON_H, "Contacts", false);

    // Swipe-up indicator — opens Settings (UI_SETTINGS). Same horizontal
    // span as the grid above it, flush with its own tap target so
    // drawSwipeIndicator() and menuSwipeBtn always agree on bounds.
    const int swipeH = 26;
    const int swipeY = row1Bottom + 10;
    menuSwipeBtn.x = outerMarginX;
    menuSwipeBtn.y = swipeY;
    menuSwipeBtn.width  = tileW * 2 + gapX;
    menuSwipeBtn.height = swipeH;
    drawSwipeIndicator(menuSwipeBtn.x, menuSwipeBtn.y, menuSwipeBtn.width, menuSwipeBtn.height, false);

    // Status row — clock / signal / battery — sits above the nav grid,
    // separated by its own hairline.

    tft.drawFastHLine(0, 26, 240, UI_BORDER);

    menuDrawn = true;
  }

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


    // LIGHT SLEEP 
    if (touched) {
      lastTouchTime = now;
    }

    if (!screenAsleep && (now - lastTouchTime >= SCREEN_SLEEP_TIMEOUT)) {
      //screenSleep(); //Using analog switch ONTOP of this -> this will be one of the sleep modes that doesent conserve as much as for example the analog button one. 
      enterLightSleep();
    }

    if (screenAsleep) {
      if (touched) {
        // screenWake(); // -> for now relying on analog switch to determine screen/touch sleep mode. 
        exitAndReset_Light(); 
      }
      return;   // swallow this loop's touch — don't let it fall through to the state machine
    }


  switch (currentState) {

    case UI_MENU:
      {

        if (justPressed && islandIsClicked(sp)){
              updateClock();
              updateCSQ();
              updateBattery();
        }

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
          // receive() draws its own receiving/result screen, so nothing
          // needs to be drawn here first.
          bool gotMessage = receive();
          while (ctpRead(sp)) delay(10);
          if (gotMessage) {
            recentMessagesReset();
            currentState = UI_MESSAGES;
          } else {
            menuDrawn = false;
            currentState = UI_MENU;
          }
          return;
        }
        

        if (justPressed && contactsBtn.isClicked(sp)) {
          tft.setCursor(0, 0);
          currentState = UI_CONTACTS;
          return;
        }

        if (justPressed && menuSwipeBtn.isClicked(sp)) {
          settingsScreenReset();
          currentState = UI_SETTINGS;
          return;
        }

        break;
      }

    case UI_SETTINGS:
      {
        drawSettingsScreen(sp, justPressed);

        if (justPressed && settingsCloseBtnPressed(sp)) {
          menuDrawn = false;  // swipe pill drew over the menu — redraw it
          currentState = UI_MENU;
          return;
        }

        break;
      }

    case UI_MESSAGES:
      {
        
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

          // Remember which conversation this is by phone number, the first
          // time it's opened — see convoNumber's declaration up top.
          const char* pickedPhone = getConversationPhone(picked);
          if (pickedPhone != nullptr) {
            strncpy(convoNumber, pickedPhone, MAX_PHONE_LEN - 1);
            convoNumber[MAX_PHONE_LEN - 1] = '\0';
          } else {
            convoNumber[0] = '\0';
          }

          conversationReset();
          currentState = UI_CONVO;
          wasTouched = true;
          return;
        }
        break;
      }

    case UI_CONVO:
      {

        drawConversationToTFT(getConversationSelectionByPhone(convoNumber), sp, justPressed, touched); // This only works if the # of convo stays the same after a RX on a specific convo tab

        if (justPressed && convoBackBtnPressed(sp)) {
          conversationReset();
          recentMessagesReset();
          currentState = UI_MESSAGES;
          wasTouched = true;
          return;
        }
        
        if (justPressed && convoRxBtnPressed(sp)) {
          // receive() draws its own receiving/result screen, so nothing
          // needs to be drawn here first.
          receive();  // return value doesn't matter here — we stay on this convo either way
          while (ctpRead(sp)) delay(10);

          conversationReset();
          currentState = UI_CONVO;
          wasTouched = true;
          return;
        }

        if (justPressed && convoReplyBtnPressed(sp)) {
          const char* phone = getConversationPhone(displayConvo);
          if (phone != nullptr) {
            strncpy(recipientNumber, phone, MAX_PHONE_LEN - 1);  // already normalized
            recipientNumber[MAX_PHONE_LEN - 1] = '\0';
            numberAquired = true;
            replyFromConvo = true;
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
            if (replyFromConvo) {
              replyFromConvo = false;
              conversationReset();
              currentState = UI_CONVO;
            } else {
              // FIX: this path used to skip keyboardReset() entirely — harmless
              // before, but keyboardReset() is also what restores portrait
              // rotation (see its comment), so skipping it here could leave the
              // rest of the app — none of which is landscape-aware — stuck
              // sideways if the user had rotated while still on this screen.
              keyboardReset();
              currentState = UI_MENU;
              menuDrawn = false;
            }
            return;
          }
          if (justPressed && rotateBtnPressed(sp)) {
            keyboardToggleRotation();
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
            if (replyFromConvo) {
              replyFromConvo = false;
              numberAquired = false;
              keyboardReset();
              conversationReset();
              currentState = UI_CONVO;
            } else {
              // FIX: see the matching comment in the !numberAquired branch —
              // this path also used to skip keyboardReset(), which is what
              // restores portrait rotation.
              keyboardReset();
              currentState = UI_MENU;
              menuDrawn = false;
            }
            return;
          } else if (justPressed && rotateBtnPressed(sp)) {
            keyboardToggleRotation();
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

            if (replyFromConvo) {
              // Fluid reply: land back on the conversation just texted
              // instead of the home menu. Sending just moved this thread to
              // the top (moveThreadToTop() in pushMessage()), so it's always
              // selection 0 now, regardless of where it sat before.
              replyFromConvo = false;
              displayConvo = 0;
              conversationReset();
              currentState = UI_CONVO;
            } else {
              currentState = UI_MENU;
              menuDrawn = false;
            }
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

  }  // end switch (currentState)
}  // end loop()