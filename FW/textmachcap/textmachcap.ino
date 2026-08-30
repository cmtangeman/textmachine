/*
  textmachcap.ino
  Touch input: FT6336U capacitive (I2C, 0x38)
  Display:     ILI9341 (SPI)
  This is the only file that handles touch within this project.
*/

// Arduino libraries
#include "Adafruit_GFX.h"      // UI rendering and extra feautres
#include "Adafruit_ILI9341.h"  // Drives the rendered commands to the touch screen
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

String readModemResponse(unsigned long timeout_ms = 2000) {
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
    return resp;
}


void receive() {

   // modemWake();

    // Is this a waste?
    SerialSARA.println("AT+CMGF=1");
    readModemResponse(1000); // wait for and consume terminating OK


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
        tft.fillScreen(ILI9341_BLACK);
        tft.setCursor(0, 0);
        tft.println("No new messages");
    }
    //modemSleep();
    //delay(500);
}


void text(const char* remoteNum, const char* message) {

  // modemWake();
  tft.fillScreen(ILI9341_BLACK);
  tft.setCursor(0, 0);


  // Signal looks good, attempt send
  tft.println("Sending...");
  sms.beginSMS(remoteNum);
  sms.print(message);
  int result = sms.endSMS();

  if (result == 1) {
    char sentTimestamp[MAX_TIMESTAMP_LEN];
    if (!getCurrentTimestamp(sentTimestamp, sizeof(sentTimestamp))) { // If our timestamp function messes up 
      copyBounded(sentTimestamp, "Unknown", sizeof(sentTimestamp));
    }
    pushMessage(remoteNum, message, OUT, sentTimestamp);
    tft.println("Sent!");
  } else {
    tft.println("Failed.");
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
  digitalWrite(TFT_CS, HIGH);
  pinMode(SD_CS, OUTPUT);
  digitalWrite(SD_CS, HIGH);

  // ── Touch controller reset ────────────────────────────
  pinMode(CTP_RST, OUTPUT);
  digitalWrite(CTP_RST, LOW);  delay(10);
  digitalWrite(CTP_RST, HIGH); delay(50);
  Wire.begin();

  // ── TFT FIRST ─────────────────────────────────────────
  // Turn screen on
  pinMode(LED_SCREEN, OUTPUT);
  digitalWrite(LED_SCREEN, HIGH);
  lastTouchTime = millis();

  tft.begin();
  tft.invertDisplay(true);
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

   // modemSleep();   // registered — now let it default to power-saving from here on

  pinMode(SLEEP_PIN, INPUT_PULLDOWN);  // Bring the pin down to 0 from ambiguos mode 

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
    tft.setTextColor(ILI9341_WHITE);
    tft.setTextSize(2);
    tft.fillScreen(ILI9341_BLACK);
    tft.setRotation(ROTATION);

    // (int xPos, int yPos, int butWidth, int butHeight, const char* butText, uint16_t butColor)
    if(unreadMessage){
    msgBtn.initButton(0, 60, 240, 40, "Messages",ILI9341_RED);
    }else{
    msgBtn.initButton(0, 60, 240, 40, "Messages");
    }
    compBtn.initButton(0, 110, 240, 40, "Compose");
    refreshBtn.initButton(0, 160, 240, 40, "Refresh");
    contactsBtn.initButton(0, 210, 240, 40, "Contacts");
   //  debugBtn.initButton(0, 260, 240, 40, "Debug");


    // Update the time and the power

    updateClock();
    updateCSQ();
    updateBattery();

    menuDrawn = true;
  }

  // If there is a notification on any of the chats, add a blue iOS style circle over the messages rectangle!



  // Check signal first


  bool touched = false;
  ScreenPoint sp;

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
          tft.fillScreen(ILI9341_BLACK);
          tft.setCursor(0, 0);
          receive();
          while (ctpRead(sp)) delay(10);
          currentState = UI_MENU;
          menuDrawn = false;
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
            keyboardSwitchToMessageField(cidx != -1 ? getContactName(cidx) : recipientNumber);

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
            keyboardSwitchToMessageField(recipientNumber);
            }else{
            keyboardSwitchToMessageField(getContactName(idx));
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
            keyboardSwitchToMessageField(name != nullptr ? name : recipientNumber);
            
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
            keyboardSwitchToMessageField(newContactPhone);  // freeze phone in To: field
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





  // Turn off the backlight and ILI9341 after 20s of inactivity.
  // Keep FT6636U running so that it can wake up after being pressed
  // Eventually an analog button might be the next step in making it so we dont need to keep the FT6636U enabled. 
void screenSleep(){
  if (screenAsleep) return;
  digitalWrite(LED_SCREEN, LOW);
  screenAsleep = true;
}

void screenWake(){
  if (!screenAsleep) return;
  digitalWrite(LED_SCREEN, HIGH);
  screenAsleep = false;
  lastTouchTime = millis();
}

void ctpSleep() {
    digitalWrite(CTP_RST, LOW);  // hold in reset = disabled
}
void ctpReset() {
    digitalWrite(CTP_RST, LOW);
    delay(5);
    digitalWrite(CTP_RST, HIGH);
    delay(50);

}


void setBacklight(uint8_t brightness) {
    analogWrite(LED_SCREEN, brightness);
}


bool modemAwake() {
  while (SerialSARA.available()) SerialSARA.read();
  SerialSARA.println("AT");
  String resp = readModemResponse(1000);
  return resp.indexOf("OK") != -1;
}



void sdSleep() {
    SD.end();                    // releases SPI bus
    pinMode(SD_CS, OUTPUT);
    digitalWrite(SD_CS, HIGH);  // deselect
}

void sdWake() {
    SD.begin(SD_CS);             // reinitialize
}


void modemSleep(){
  SerialSARA.println("AT+CPSMS=1");
  readModemResponse(1000);   // consume the OK so it doesn't sit in the buffer for next time
}

void modemWake() {
  if (modemAwake()) {
    Serial.println("Modem already responsive.");
    return;
  }

  Serial.println("No response — pulsing PWR_ON.");
  digitalWrite(SARA_PWR_ON, HIGH);
  delay(PWR_ON_PULSE_MS);
  digitalWrite(SARA_PWR_ON, LOW);
  delay(500);

  if (modemAwake()) {
    Serial.println("Modem woke via PWR_ON.");
    SerialSARA.println("AT+CPSMS=0");
    readModemResponse(1000);
  } else {
    Serial.println("Modem still unresponsive after PWR_ON pulse.");
    // Per forum guidance: don't drive RESETN as a fallback here — risk outweighs benefit.
    // If this fires, it's worth surfacing to the UI as a real error state rather than retrying blindly.
  }
}


void exitAndReset() {
    //sdWake();
    
    SPI.begin();    // SPI bus lost clock during standby
    Wire.begin();   // I2C same
    
    //sdWake();       // SD needs SPI working first
    ctpReset();     // touch needs I2C working first
    delay(100);
    screenWake();   // backlight on

    
    // reset millis() based timers since they stopped during sleep
    lastTouchTime   = millis();
    lastCSQUpdate   = millis();
    lastClockUpdate = millis();
    
}

void enterSleep() {
    screenSleep();
    ctpSleep();
    //sdSleep();
    
    // attach interrupt to SLEEP_PIN — wake when it goes LOW
    LowPower.attachInterruptWakeup(SLEEP_PIN, exitAndReset, FALLING);
    
    // put CPU into standby — ~2μA
    LowPower.deepSleep();
    
    
    // execution resumes here after wake
}
