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


/*
// Calibrated Value : xCalM = -0.09, xCalC = 333.49yCalM = -0.06, yCalC = 250.6
#include "SPI.h"
#include "Adafruit_GFX.h" // -> 
#include "Adafruit_ILI9341.h" // -> Dislpay chip lib
#include "XPT2046_Touchscreen.h" // -> Touch chip lib 
#include "Math.h"



// ---------- Connections between screen and arduino ----------
#define TFT_DC   6
#define TFT_CS   7
#define TFT_RST -1

#define TS_CS 5


#define ROTATION 3


// Use hardware SPI (on Uno, #13, #12, #11) and the above for CS/DC/RST
Adafruit_ILI9341 tft = Adafruit_ILI9341(TFT_CS, TFT_DC, TFT_RST);
XPT2046_Touchscreen ts(TS_CS);

// calibration values
float xCalM = -0.09, yCalM = -0.06; // gradients
float xCalC = 333.49, yCalC = 250.6; // y axis crossing points

int8_t blockWidth = 20; // block size
int8_t blockHeight = 20;
int16_t blockX = 0, blockY = 0; // block position (pixels)
*/



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

void setup() {
  // initialize serial communications and wait for port to open:
  Serial.begin(9600);
  while (!Serial) {
    ; // wait for serial port to connect. Needed for native USB port only
  }

  // connection state
  bool connected = false;
  // Start NB module
  // If SIM has PIN, pass it as a parameter of begin() in quotes
  while (!connected) {
    if (nbAccess.begin("") == NB_READY) {
      connected = true;
    } else {
      Serial.println("Not connected");
      delay(1000);
    }
  }
  Serial.println("CrankText - NB initialized");
}

// Prototyping 
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

    // Get remote number
    sms.remoteNumber(senderNumber, 20);
    Serial.println(senderNumber);

    // Read message bytes and print them
    while ((c = sms.read()) != -1 && i < 299) {
      senderBody[i++] = (char) c;
      Serial.print((char)c);
    }

    pushMessage(senderNumber, senderBody, IN); 
    // storeIncomingMessage(senderNumber, senderBody);
    Serial.println("\nEND OF MESSAGE");

    // Delete message from modem memory
    sms.flush();
    Serial.println("MESSAGE DELETED FROM MODEM MEMORY SAVED IN FW/");
  }

  delay(1000);

}



void text() {

Serial.println("Enter phone number");
char remoteNum[20];
readSerial(remoteNum);
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
  
  char txtMsg[500];
  readSerial(txtMsg);
  Serial.println(txtMsg);

  // send the message
  sms.beginSMS(remoteNum);
  sms.print(txtMsg);
  sms.endSMS();
  pushMessage(remoteNum, txtMsg, OUT);
  Serial.println("\nSent\n");
}
void loop(){
messagesMenu();
}