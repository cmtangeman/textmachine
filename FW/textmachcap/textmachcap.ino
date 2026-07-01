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

#include <SD.h>
#include "contacts.h"

static bool kbDrawnFlag = false;
enum ContactStep { CONTACT_NAME,
                   CONTACT_PHONE };
ContactStep contactStep = CONTACT_NAME;
static char newContactName[30];
static char newContactPhone[MAX_PHONE_LEN];

#define SD_CS 3

#define ROTATION 0

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

// ── Touch ─────────────────────────────────────────────────────────
// FT6336U outputs pixel coordinates directly — no calibration needed.

bool ctpRead(ScreenPoint& sp) {
  Wire.beginTransmission(FT6336U_ADDR);
  Wire.write(FT_REG_NUMTOUCHES);
  Wire.endTransmission(false);
  Wire.requestFrom(FT6336U_ADDR, 5);  // Request 5 bytes from 0x38

  if (Wire.available() < 5) return false;  // Malfunction

  uint8_t touches = Wire.read();  //
  if (touches == 0 || touches > 2) return false;
  // & -> bitwise AND 0's out the top nibble and keeps all 1's from bottom niblle ( onlu the bottom nibble contains screen position, top is flags etc.)
  uint16_t x = ((Wire.read() & 0x0F) << 8) | Wire.read();  // Masks upper nibble of byte and then uses bitwise OR to combine
  uint16_t y = ((Wire.read() & 0x0F) << 8) | Wire.read();

  sp = ScreenPoint((int16_t)x, (int16_t)y);
  return true;
}

// ── NB / SMS helpers ──────────────────────────────────────────────

// Set everything in terms of E. 164 the standard format for international telephone numbers. 
// Correct U.S not using +1 or 1 and otherwise treat as EUROPEAN number with correct country code
// Ignore + input for all and concatenate to first digit for all as well.



void receive() {
    SerialSARA.println("AT+CMGF=1");
    delay(200);
    while (SerialSARA.available()) SerialSARA.read(); // Wait until ready 

    SerialSARA.println("AT+CMGL=\"REC UNREAD\"");
    delay(1000);

    //Read entire response first 
    String fullResponse = "";
    while (SerialSARA.available()) {
        char c = (char)SerialSARA.read();
        fullResponse += c;
    }

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
    delay(500);
}


void text(const char* remoteNum, const char* message) {
  tft.fillScreen(ILI9341_BLACK);
  tft.setCursor(0, 0);


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

enum UiState { UI_MENU,
               UI_MESSAGES,
               UI_REFRESH,
               UI_COMPOSE,
               UI_CONVO,
               UI_CONTACTS,
               UI_ADD_CONTACT,
               UI_DEBUG };


UiState currentState = UI_MENU;

// ── Setup ─────────────────────────────────────────────────────────

void setup() {
  Serial.begin(115200);
  while (!Serial && millis() < 3000)
    ;

  pinMode(TFT_CS, OUTPUT);
  digitalWrite(TFT_CS, HIGH);
  pinMode(SD_CS, OUTPUT);
  digitalWrite(SD_CS, HIGH);

  int retries = 3;
  while (retries--) {
    if (SD.begin(SD_CS)) {
      Serial.println("SD ready");
      loadContactsFromSD();
      break;
    }
    Serial.println("SD init failed, retrying...");
    delay(500);
  }
  if (!SD.exists("contacts.csv")) {
    File f = SD.open("contacts.csv", FILE_WRITE);
    if (f) {
      f.println("Charlie|+14156100909");
      f.println("Dad|+14156522835");
      f.println("Oliver|+14156100910");
      f.close();
      Serial.println("contacts.csv created");
    } else {
      Serial.println("Failed to create contacts.csv");
    }
  }
  if (!SD.exists("messages.csv")) {


  // Reset touch controller
  pinMode(CTP_RST, OUTPUT);
  digitalWrite(CTP_RST, LOW);
  delay(10);
  digitalWrite(CTP_RST, HIGH);
  delay(50);
  Wire.begin();

  tft.begin();
  tft.setRotation(ROTATION);
  tft.fillScreen(ILI9341_RED);
  tft.invertDisplay(true);





  bool connected = false;
  while (!connected) {
    if (nbAccess.begin("") == NB_READY) {  // Does unsigned long baud = 115200; SerialSARA.begin(baud); internally

      connected = true;
      tft.fillScreen(ILI9341_BLACK);

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
      // SerialSARA.println("AT+CPSMS=0");
      // SerialSARA.println("AT+CPSMS=1,,,\"00000001\",\"00001010\"");

    } else {
      tft.fillScreen(ILI9341_BLACK);
      tft.println("Not connected");
      Serial.println("Not connected");
      delay(1000);
    }
  }
}
}

// ── Loop ──────────────────────────────────────────────────────────

void loop() {

  unsigned long now = millis();



  static Button msgBtn, compBtn, refreshBtn, contactsBtn, backBtn, debugBtn;
  static bool isDrawnConvo = false;




  if (!menuDrawn) {
    tft.setTextColor(ILI9341_WHITE);
    tft.setTextSize(2);
    tft.fillScreen(ILI9341_BLACK);
    tft.setRotation(ROTATION);

    // (int xPos, int yPos, int butWidth, int butHeight, const char* butText, uint16_t butColor)
    msgBtn.initButton(0, 60, 240, 40, "Messages");
    compBtn.initButton(0, 110, 240, 40, "Compose");
    refreshBtn.initButton(0, 160, 240, 40, "Refresh");
    contactsBtn.initButton(0, 210, 240, 40, "Contacts");
    debugBtn.initButton(0, 260, 240, 40, "Debug");


    // Update the time and the power

    updateClock();
    updateCSQ();
    updateBattery();

    menuDrawn = true;
  }

  // Check signal first


  bool touched = false;
  ScreenPoint sp;

  if (ctpRead(sp)) {
    touched = true;
  }

  static bool wasTouched = false;
  bool justPressed = touched && !wasTouched;
  wasTouched = touched;


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




    default: break;
  }
}