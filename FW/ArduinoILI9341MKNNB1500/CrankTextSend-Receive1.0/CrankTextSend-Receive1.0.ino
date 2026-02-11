/*

Crank Text O.S 

This sketch, for the MKR NB 1500 board, can:

Send messages, receive messages, and save contacts and display contact information all through serial monitor. 


 Circuit:
 * MKR NB 1500 board
 * Antenna: Ublox Dipole
 * SIM card: Tello Mobile 5$/month unlimited text 
 * Display : Sparkfun resitive LCD dislpay with ILI9341/XPT2046 display/touch chips



*/

#include "SPI.h"
#include "Adafruit_GFX.h" // -> 
#include "Adafruit_ILI9341.h" // -> Dislpay chip lib
#include "XPT2046_Touchscreen.h" // -> Touch chip lib 
#include "Math.h"

#include "buttons.h"
#include "touchCal.h"



// ---------- Connections between screen and arduino ----------
#define TFT_DC   6
#define TFT_CS   7
#define TFT_RST -1

#define TS_CS 5


#define ROTATION 2

// Use hardware SPI (on Uno, #13, #12, #11) and the above for CS/DC/RST
Adafruit_ILI9341 tft = Adafruit_ILI9341(TFT_CS, TFT_DC, TFT_RST);
XPT2046_Touchscreen ts(TS_CS);

Button button;

// ------------------------------------------------------------
// Include the NB library
#include <stdio.h>


// NB Library and setup
#include <MKRNB.h>
#include "contacts.h"
#include "Messages.h"

// Some sim cards will require a Pin number to initializ NB. If so pass as :
// const char PINNUMBER[] = SECRET_PINNUMBER;

// initialize the library instance
NB nbAccess;
NB_SMS sms;
// ------------------------------------------------------------

#include <SD.h>

#define SD_CS 0


void setup() {

  

  tft.begin();                 // initialize ILI9341 display
  tft.setRotation(ROTATION);      // Orientation
  tft.fillScreen(ILI9341_BLACK);

  ts.begin();                  // Initialize XPT2046 touch
  ts.setRotation(ROTATION);

  tft.setTextColor(ILI9341_WHITE, ILI9341_BLACK);
  tft.setTextSize(2);

  // initialize serial communications and wait for port to open:
  Serial.begin(9600);
  while (!Serial) {
    ; // wait for serial port to connect. Needed for native USB port only
  }

  if(!SD.begin(SD_CS)) {
    Serial.println("SD FAIL");
  }

  Serial.println("SD OK");
  File f = SD.open("/boot.txt", FILE_WRITE);
  f.println("CrankText boot OK");
  f.close();

  // connection state
  bool connected = false;
  // Start NB module
  // If SIM has PIN, pass it as a parameter of begin() in quotes
  while (!connected) {
    if (nbAccess.begin("") == NB_READY) {
      connected = true;
    } else {
      tft.println("Not connected");
      delay(1000);
    }
  }

  Serial.print(F("Circles (filled)         "));
  Serial.println(testFilledCircles(10, ILI9341_MAGENTA));

 

}

  unsigned long testFilledCircles(uint8_t radius, uint16_t color) {
  unsigned long start;
  int x, y, w = tft.width(), h = tft.height(), r2 = radius * 2;

  tft.fillScreen(ILI9341_BLACK);
  start = micros();
  for(x=radius; x<w; x+=r2) {
    for(y=radius; y<h; y+=r2) {
      tft.fillCircle(x, y, radius, color);
    }
  }

  return micros() - start;
}

int readSerial(char result[]) {
  int i = 0;
  while (1) {
    while (Serial.available() > 0) {
      char inChar = Serial.read();
      if (inChar == '\n') {
        result[i] = '\0';
        Serial.flush();
        return 0;
      }
      if (inChar != '\r') {
        result[i] = inChar;
        i++;
      }
    }
  }
}

void receive() {
  int c;
  char senderNumber[20];
  char senderBody[300];
  int i = 0;

  // If there are any SMSs available()
  if (sms.available()) {
    Serial.println("Message received from:");
    tft.println("Message received from:");

    // Get remote number
    sms.remoteNumber(senderNumber, 20);
    Serial.println(senderNumber);
    tft.println(senderNumber);

    // Read message bytes and print them
    while ((c = sms.read()) != -1 && i < 299) {
      senderBody[i++] = (char)c;
      Serial.print((char)c);
      tft.print((char)c);
    }

    senderBody[i] = '\0';

    pushMessage(senderNumber, senderBody, IN); 
    // storeIncomingMessage(senderNumber, senderBody);
    Serial.println("\nEND OF MESSAGE");
    tft.println("\nEND OF MESSAGE");

    // Delete message from modem memory
    sms.flush();
    Serial.println("MESSAGE DELETED FROM MODEM MEMORY SAVED IN FW/");
    tft.println("MESSAGE DELETED FROM MODEM MEMORY SAVED IN FW/");
  } else {
    Serial.println("No new messages");
    tft.println("No new messages");
  }

  delay(1000);
}


void text(char* remoteNum) {


  /*
// 1) Check if input matches a contact
int idx = findContact(remoteNum);
if (idx >= 0) {
    const char* phonePtr = getContactPhone(idx);
    if (phonePtr) {
        strncpy(remoteNum, phonePtr, sizeof(remoteNum));
        remoteNum[sizeof(remoteNum) - 1] = '\0';
    }
    return;
}else if(isNumber){
    Serial.println(remoteNum);
}else{
    Serial.println("Not a valid number or contact");
    messagesMenu();
}
*/


  // SMS text
  Serial.print("Now, enter SMS content: ");
  tft.print("Now, enter SMS content: ");
  
  char txtMsg[500];
  readSerial(txtMsg);

  Serial.println(txtMsg);
  tft.println(txtMsg);

  // send the message
  sms.beginSMS(remoteNum);
  sms.print(txtMsg);
  sms.endSMS();
  pushMessage(remoteNum, txtMsg, OUT);

  Serial.println("\nSent\n");
  tft.println("\nSent\n");
}



void loop() {
  ScreenPoint sp;
  bool touched = readTouch(sp);

  uiTick(sp, touched);   // or messagesMenu(sp, touched) if you haven't renamed yet
}

