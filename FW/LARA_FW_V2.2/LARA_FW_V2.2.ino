/*************************************************** 
  Hardware Art Lara FW.  
  Original Cellular FW by Adafruit as disclaimered below

  Things to do in rough order:
  0.  FONA library modifications as needed for #1,#2
  1.  Remap for new UART I/O
  2.  Remap for new Keypad I/O
  3.  Keypad Testing
  4.  Major revamp for POR for LARA vs old 2G
  5.  New commands for Lara config.
  6.  Attempt text
  7.  Audio config
  8.  Attempt call
 
 ****************************************************/

/*************************************************** 
  This is an example for our Adafruit FONA Cellular Module

  Designed specifically to work with the Adafruit FONA 
  ----> http://www.adafruit.com/products/1946
  ----> http://www.adafruit.com/products/1963

  These displays use TTL Serial to communicate, 2 pins are required to 
  interface
  Adafruit invests time and resources providing this open source code, 
  please support Adafruit and open-source hardware by purchasing 
  products from Adafruit!

  Written by Limor Fried/Ladyada for Adafruit Industries.  
  BSD license, all text above must be included in any redistribution
 ****************************************************/

/* 
Open up the serial console on the Arduino at 115200 baud to interact with LARA
*/
  
#include "Arduino.h"
#include <Keypad.h>

#include "HA_FONA_LARA.h"

#define FONA_RX 8
#define FONA_TX 9
#define FONA_RTS 11
#define FONA_RST 4
#define FONA_PWR_ON 1
//not used on LTE #define FONA_RI 7

/*#include <SoftwareSerial.h>
SoftwareSerial ha_fonaSS = SoftwareSerial(FONA_TX, FONA_RX);
SoftwareSerial *fonaSerial = &ha_fonaSS;
*/

//vcc
unsigned vcc;

//keypad start
const byte ROWS = 5; //five rows
const byte COLS = 3; //three columns
char keys[ROWS][COLS] = {
{'1','2','3'},
{'4','5','6'},
{'7','8','9'},
{'*','0','#'},
{'<','H','>'}
};
//Micro PIN         A3  A1  PC5
//  Keypad Pinout   6   4   2
//    Schematic     A5  A3  A1
//A10   1   A0      1   2   3
//A0    3   A2      4   5   6
//A2    5   A4      7   8   9
//A4    7   A6      *   0   #
//A5    8   A7      <   H   >
//byte rowPins[ROWS] = {10, 5, A1, A3, A5}; //connect to the row pinouts of the kpd
byte rowPins[ROWS] = {A5, A3, A1, 5, 10}; //connect to the row pinouts of the kpd
byte colPins[COLS] = {A0, A2, A4}; //connect to the column pinouts of the kpd
//LTE:  see page 142 of notebook for above mapping
//bad LTE byte rowPins[ROWS] = {A5, A4, A2, A0, 10}; //connect to the row pinouts of the kpd
//bak LTE byte colPins[COLS] = {A3, A1, 5}; //connect to the column pinouts of the kpd
//2G keypad: byte rowPins[ROWS] = {A3, A4, A5, 10}; //connect to the row pinouts of the kpd
//2G keypad: byte colPins[COLS] = {A0, A1, A2}; //connect to the column pinouts of the kpd
//byte rowPins[ROWS] = {18, 14, 16, 15}; //connect to the row pinouts of the kpd
//byte colPins[COLS] = {19, 10, 17}; //connect to the column pinouts of the kpd
//byte rowPins[ROWS] = {a4=18, a0=14, a2=16, a1=15}; //connect to the row pinouts of the kpd
//byte colPins[COLS] = {a5=19, 10, a3=17}; //connect to the column pinouts of the kpd

Keypad kpd = Keypad( makeKeymap(keys), rowPins, colPins, ROWS, COLS );

unsigned long startTime;
String msg;
char knumber[30] ;//= {'1','4','1','5','6','5','2','2','8','3','5'};

int  iknumber = 0; 
int  vol_mode = 0;
int  vol_value = 5;
int iknumber_int ;
int incall = 0;
boolean incoming_call;
char command = 0;
char break_to_menu = 0;




//keypad end
// this is a large buffer for replies
char replybuffer[255];

// or comment this out & use a hardware serial port like Serial1 (see below)
//SoftwareSerial ha_fonaSS = SoftwareSerial(FONA_TX, FONA_RX);

HA_FONA_LARA ha_fona = HA_FONA_LARA(FONA_RST);

uint8_t readline(char *buff, uint8_t maxbuff, uint16_t timeout = 0);
    char my_ps = 5;
/*
 * 
 * https://www.arduino.cc/en/Hacking/PinMapping32u4
 * 
  _BV(2), // D0 - PD2
  _BV(3), // D1 - PD3
  _BV(1), // D2 - PD1
  _BV(0), // D3 - PD0
  _BV(4), // D4 - PD4
  _BV(6), // D5 - PC6
  _BV(7), // D6 - PD7
  _BV(6), // D7 - PE6

  _BV(4), // D8 - PB4
  _BV(5), // D9 - PB5
  _BV(6), // D10 - PB6
  _BV(7), // D11 - PB7
  _BV(6), // D12 - PD6
  _BV(7), // D13 - PC7

  _BV(3), // D14 - MISO - PB3
  _BV(1), // D15 - SCK - PB1
  _BV(2), // D16 - MOSI - PB2
  _BV(0), // D17 - SS - PB0

  _BV(7), // D18 - A0 - PF7
  _BV(6), // D19 - A1 - PF6
  _BV(5), // D20 - A2 - PF5
  _BV(4), // D21 - A3 - PF4
  _BV(1), // D22 - A4 - PF1
  _BV(0), // D23 - A5 - PF0

  _BV(4), // D24 / D4 - A6 - PD4
  _BV(7), // D25 / D6 - A7 - PD7
  _BV(4), // D26 / D8 - A8 - PB4
  _BV(5), // D27 / D9 - A9 - PB5
  _BV(6), // D28 / D10 - A10 - PB6
  _BV(6), // D29 / D12 - A11 - PD6
  */
void setup() {
//vcc
  ADMUX = _BV(REFS0) | _BV(MUX4) | _BV(MUX3) | _BV(MUX2) | _BV(MUX1);
  ADCSRB = 0;
//endvcc  
  MCUCR = (1<<JTD);
    MCUCR = (1<<JTD);
  pinMode(13, OUTPUT);
  digitalWrite(13, HIGH);   // turn the LED on (HIGH is the voltage level)
  pinMode(FONA_RTS,OUTPUT);
  digitalWrite(FONA_RTS, LOW);  //Enable whenever UART is in use
  Serial.begin(4800);
// smt 12/19/2020: can't check when USB is disconnected:  while (!Serial) ;
//  delay(1000);              // wait 
  startTime = millis();
  
  ha_fonaSS.begin(9600); // if you're using software serial

  // See if the FONA is responding
  while (! ha_fona.begin(ha_fonaSS)) {           // can also try fona.begin(Serial1) 
    Serial.println(F("Couldn't find HA FONA"));
        delay(1000);              // wait
  }
  
  Serial.println(F("HA Phone1 is OK"));
        delay(1000);              // wait 
        digitalWrite(13, LOW);    // turn the LED off by making the voltage LOW
        if (!ha_fona.setGPIO(16,2,0)) { // turn on GPIO1 for netstat
          //Serial.println(F("GPIO 1 Set Failed"));
          Serial.println(F(" "));
        } else {
          Serial.println(F("GPIO 1 Set!"));
        }        
        if (!ha_fona.setGPIO(23,0,1)) {  // turn on GPIO2 for battery charge 
          //Serial.println(F("GPIO 1 Set Failed"));
          Serial.println(F(" "));
        } else {
          Serial.println(F("GPIO 1 Set!"));
        }        
        
   //     printTestMenu();  // take this out in production so as not to fill up the UART when not plugged to USB.

}


void printTestMenu(void) {
  Serial.println(F("-------------------------------------\n"));
  Serial.println(F("[&] Test menu\n"));
  Serial.println(F("SMS Functions:"));
  Serial.println(F("[N] Number of SMSs"));
  Serial.println(F("[r] Read SMS #"));
  Serial.println(F("[R] Read All SMS"));
  Serial.println(F("[d] Delete SMS #"));
  Serial.println(F("[s] Send SMS"));
  Serial.println(F("[u] Send USSD\n"));
  Serial.println(F("Test Functions:"));
  Serial.println(F("\tPhone:"));
  Serial.println(F("[c] make phone Call"));
  Serial.println(F("[A] get call status"));
  Serial.println(F("[h] Hang up phone"));
  Serial.println(F("[p] Pick up phone\n"));
  Serial.println(F("\tGeneral:"));  
  Serial.println(F("[O] Tone Test"));
  Serial.println(F("[D] DTMF Tone During Call"));
  Serial.println(F("[S] Create Serial Tube"));
  Serial.println(F("[Y] Init Config"));
  Serial.println(F("[a] Audio Config"));
  Serial.println(F("[B] Check Audio Config"));
  Serial.println(F("[k] Enter into normal Keypad operation"));
  Serial.println(F("[i] get IMEI number"));
  Serial.println(F("[I] get APN"));
  Serial.println(F("[J] write ATT IoT APN"));
  Serial.println(F("[n] Check Network Status"));
  Serial.println(F("[Z] Get RSSI"));
  Serial.println(F("[C] get SIM card ID"));
  Serial.println(F("[g] Set a pin number high"));
  Serial.println(F("[l] Set a pin number low"));
  Serial.println(F("[t] Toggle a pin number high/low_1second/high"));
  Serial.println(F("[T] Toggle a pin number high/low_200millis/high"));

  Serial.println(F("-------------------------------------"));
  Serial.println(F(""));

}
void loop() {

//    Serial.print(F("HA-PHONE1> "));
//    Serial.print(F("Type '&' to view Test menu\n"));
//    while (! Serial.available() );  
//    command = Serial.read();
//    Serial.println(command);    

  if ((break_to_menu == 'x')||(break_to_menu == 'X')){
    Serial.print(F("HA-PHONE1> "));
    Serial.print(F("Type '?' to view SMS mode, or '&' to view SMS & Test modes menu\n"));
    while (! Serial.available() );  
    command = Serial.read();
    Serial.println(command);    
  }
  else
  {
    command = 'k';
  }  
  
  switch (command) {
    case '&': {
        printTestMenu();
        break;
      }

    /*** SMS ***/

    case 'N': {
        // read the number of SMS's!
        int8_t smsnum = ha_fona.getNumSMS();
        if (smsnum < 0) {
          Serial.println(F("Could not read # SMS"));
        } else {
          Serial.print(smsnum);
          Serial.println(F(" SMS's on SIM card!"));
        }
        break;
      }
    case 'r': {
        // read an SMS
        flushSerial();
        Serial.print(F("Read #"));
        uint8_t smsn = readnumber();
        Serial.print(F("\n\rReading SMS #")); Serial.println(smsn);

        // Retrieve SMS sender address/phone number.
        if (! ha_fona.getSMSSender(smsn, replybuffer, 250)) {
          Serial.println("Failed!");
          break;
        }
        Serial.print(F("FROM: ")); Serial.println(replybuffer);

        // Retrieve SMS value.
        uint16_t smslen;
        if (! ha_fona.readSMS(smsn, replybuffer, 250, &smslen)) { // pass in buffer and max len!
          Serial.println("Failed!");
          break;
        }
        Serial.print(F("***** SMS #")); Serial.print(smsn);
        Serial.print(" ("); Serial.print(smslen); Serial.println(F(") bytes *****"));
        Serial.println(replybuffer);
        Serial.println(F("*****"));

        break;
      }
    case 'R': {
        // read all SMS
        int8_t smsnum = ha_fona.getNumSMS();
        uint16_t smslen;
        int8_t smsn;

 //       if ( (type == FONA3G_A) || (type == FONA3G_E) ) {
 //         smsn = 0; // zero indexed
 //         smsnum--;
 //       } else {
          smsn = 1;  // 1 indexed
 //       }

        for ( ; smsn <= smsnum; smsn++) {
          Serial.print(F("\n\rReading SMS #")); Serial.println(smsn);
          if (!ha_fona.readSMS(smsn, replybuffer, 250, &smslen)) {  // pass in buffer and max len!
            Serial.println(F("Failed!"));
            break;
          }
          // if the length is zero, its a special case where the index number is higher
          // so increase the max we'll look at!
          if (smslen == 0) {
            Serial.println(F("[empty slot]"));
            smsnum++;
            continue;
          }

          Serial.print(F("***** SMS #")); Serial.print(smsn);
          Serial.print(" ("); Serial.print(smslen); Serial.println(F(") bytes *****"));
          Serial.println(replybuffer);
          Serial.println(F("*****"));
        }
        break;
      }

    case 'd': {
        // delete an SMS
        flushSerial();
        Serial.print(F("Delete #"));
        uint8_t smsn = readnumber();

        Serial.print(F("\n\rDeleting SMS #")); Serial.println(smsn);
        if (ha_fona.deleteSMS(smsn)) {
          Serial.println(F("OK!"));
        } else {
          Serial.println(F("Couldn't delete"));
        }
        break;
      }

    case 's': {
        // send an SMS!
        char sendto[21], message[141];
        flushSerial();
        Serial.print(F("Send to #"));
        readline(sendto, 20);
        Serial.println(sendto);
        Serial.print(F("Type out one-line message (140 char): "));
        readline(message, 140);
        Serial.println(message);
        if (!ha_fona.sendSMS(sendto, message)) {
          Serial.println(F("Failed"));
        } else {
          Serial.println(F("Sent!"));
        }

        break;
      }

    case 'u': {
      // send a USSD!
      char message[141];
      flushSerial();
      Serial.print(F("Type out one-line message (140 char): "));
      readline(message, 140);
      Serial.println(message);

      uint16_t ussdlen;
      if (!ha_fona.sendUSSD(message, replybuffer, 250, &ussdlen)) { // pass in buffer and max len!
        Serial.println(F("Failed"));
      } else {
        Serial.println(F("Sent!"));
        Serial.print(F("***** USSD Reply"));
        Serial.print(" ("); Serial.print(ussdlen); Serial.println(F(") bytes *****"));
        Serial.println(replybuffer);
        Serial.println(F("*****"));
      }
    }
/*
    case 'O': {
 //AT+UTGN=1000,1000,100,1
//Tone freq, tone length in ms, tone vol 0-100, (path: 0 downlink only, 1 uplink only, 2 both)
//G, A, F, (octave lower) F, C

      if (!ha_fona.playTone_LTE(1567, 1000, 100, 2)) {
          Serial.println(F("Failed"));
        } else {
          Serial.println(F("Done!"));
        }
      if (!ha_fona.playTone_LTE(1760, 1000, 100, 2)) {
          Serial.println(F("Failed"));
        } else {
          Serial.println(F("Done!"));
        }
      if (!ha_fona.playTone_LTE(1396, 1000, 100, 2)) {
          Serial.println(F("Failed"));
        } else {
          Serial.println(F("Done!"));
        }
      if (!ha_fona.playTone_LTE(698, 1000, 100, 2)) {
          Serial.println(F("Failed"));
        } else {
          Serial.println(F("Done!"));
        }
      if (!ha_fona.playTone_LTE(1046, 1000, 100, 2)) {
          Serial.println(F("Failed"));
        } else {
          Serial.println(F("Done!"));
        }

      if (!ha_fona.playToolkitTone(1046, 1000, 100, 2)) {
          Serial.println(F("Failed"));
        } else {
          Serial.println(F("Done!"));
        }

        break;
      }
*/
    case 'D': {
        if (!ha_fona.playDTMF(2)) {
          Serial.println(F("Failed"));
        } else {
          Serial.println(F("Done!"));
        }

        break;
      }


    case 'Y': {
        if (!ha_fona.initConfig(2)) {
          Serial.println(F("Failed"));
        } else {
          Serial.println(F("Done!"));
        }

        break;
      }

    case 'a': {
        if (!ha_fona.audioConfig(2)) {
          Serial.println(F("Failed"));
        } else {
          Serial.println(F("Done!"));
        }

        break;
      }

    case 'B': {
        if (!ha_fona.checkAudioConfig(2)) {
          Serial.println(F("Failed"));
        } else {
          Serial.println(F("Done!"));
        }

        break;
      }

    case 'Z': {
        // read the RSSI
        uint8_t n = ha_fona.getRSSI();
        int8_t r;

        Serial.print(F("RSSI = ")); Serial.print(n); Serial.print(": ");
        if (n == 0) r = -115;
        if (n == 1) r = -111;
        if (n == 31) r = -52;
        if ((n >= 2) && (n <= 30)) {
          r = map(n, 2, 30, -110, -54);
        }
        Serial.print(r); Serial.println(F(" dBm"));

        break;
      }
      
      case 'n': {
        // read the network/cellular status
        uint8_t n = ha_fona.getNetworkStatus();
        Serial.print(F("Network status "));
        Serial.print(n);
        Serial.print(F(": "));
        if (n == 0) Serial.println(F("Not registered"));
        if (n == 1) Serial.println(F("Registered (home)"));
        if (n == 2) Serial.println(F("Not registered (searching)"));
        if (n == 3) Serial.println(F("Denied"));
        if (n == 4) Serial.println(F("Unknown"));
        if (n == 5) Serial.println(F("Registered roaming"));
        if (n == 6) Serial.println(F("registeredfor SMSonly,homenetwork(applicable only when <AcTStatus> indicates E-UTRAN)"));
        if (n == 7) Serial.println(F("registeredforSMSonly,roaming(applicableonlywhen<AcTStatus>indicatesE-UTRAN)"));
        if (n == 8) Serial.println(F("attached for emergency bearer services only(see3GPPTS24.008[12]and3GPP TS 24.301 [69] that specify the condition when the MS is considered as attached for emergency bearer services)"));
        if (n == 9) Serial.println(F("registered for CSFBnotpreferred,homenetwork(applicableonlywhen <AcTStatus> indicates E-UTRAN)"));
        if (n == 10) Serial.println(F("registered for CSFBnotpreferred,roaming(applicableonlywhen<AcTStatus> indicates E-UTRAN)"));
        break;
      }
    case 'C': {
        // read the CCID
        ha_fona.getSIMCCID(replybuffer);  // make sure replybuffer is at least 21 bytes!
        Serial.print(F("SIM CCID = ")); Serial.println(replybuffer);
        break;
      }
    case 'i': {
      flushSerial();
          // Print module IMEI number.
      char imei[15] = {0}; // MUST use a 16 character buffer for IMEI!
      uint8_t imeiLen = ha_fona.getIMEI(imei);
     if (imeiLen > 0) {
      Serial.print("Module IMEI: "); Serial.println(imei);
      }
      break;
    }

    case 'I': {
      flushSerial();
          // Switch to ATT IoT SIM Card use.
      char apn[28] = "";
      uint8_t apnLen = ha_fona.getAPN(apn);
     if (apnLen > 0) {
      Serial.print("Module APN: "); Serial.println(apn);
      }
      break;
    }

    case 'J': {
      flushSerial();
          // Switch to ATT IoT SIM Card use.
      char apn[13] = "m2m64.com.attz";
      //Serial.println(apn);
      uint8_t apnLen = ha_fona.setAPN(apn);
 //    if (apnLen > 0) {
 //     Serial.print("Module APN: "); Serial.println(apn);
      
      break;
    }


    /*** Call ***/
    case 'c': {
        // call a phone!
        char number[30];
        flushSerial();
        Serial.print(F("Call #"));
        readline(number, 30);
        Serial.println();
        Serial.print(F("Calling ")); Serial.println(number);
        if (!ha_fona.callPhone(number)) {
          Serial.println(F("Failed"));
        } else {
          Serial.println(F("Sent!"));
        }

        break;
      }
    case 'A': {
        // get call status
        int8_t callstat = ha_fona.getCallStatus();
        switch (callstat) {
          case 0: Serial.println(F("Ready")); break;
          case 1: Serial.println(F("Could not get status")); break;
          case 3: Serial.println(F("Ringing (incoming)")); break;
          case 4: Serial.println(F("Ringing/in progress (outgoing)")); break;
          default: Serial.println(F("Unknown")); break;
        }
        break;
      }
      
    case 'h': {
        // hang up!
        if (! ha_fona.hangUp()) {
          Serial.println(F("Failed"));
        } else {
          Serial.println(F("OK!"));
        }
        break;
      }

    case 'p': {
        // pick up!
        if (! ha_fona.pickUp()) {
          Serial.println(F("Failed"));
        } else {
          Serial.println(F("OK!"));
        }
        break;
      } 
 
case 'k': 
case 'K': {
//Weekender Start

unsigned long   startTime = millis();
String          msg = "";
uint16_t        vbat;

flushSerial();
/* . fona need mapping
// Set Audio to External output
if (! fona.setAudio(FONA_EXTAUDIO)) { Serial.println(F("Failed"));} 
else { Serial.println(F("OK!"));  }

if (! fona.setMicVolume(FONA_EXTAUDIO, 15)) { Serial.println(F("Failed"));} 
else { Serial.println(F("OK!"));  }

// Set Volume to 5 as default
fona.setVolume(5);
 end fona need mapping */
delay(1000);

Serial.print(F("\nEntering Normal Phone Operation..."));
Serial.print(F("type 'x' to exit back to SMS and test modes\n"));
flushSerial();


// Enter continuous scan of keypad
while (1)/*(msg != " RELEASED.")*/
{

if (kpd.getKeys()) // returns 1 when there is ANY keypad activity
    {
        for (int i=0; i<LIST_MAX; i++)   // Scan the whole key list.
        {
            if ( kpd.key[i].stateChanged )   // Only find keys that have changed state.
            {
                switch (kpd.key[i].kstate) {  // Report active key state : IDLE, PRESSED, HOLD, or RELEASED
                    case PRESSED:
                    msg = " PRESSED.";
                    // need mapping    fona.playToolkitTone(1, 250);
                    ha_fona.playTone_LTE(1567, 500, 100, 2);
                  break;
                  
                    case HOLD:
                    msg = " HOLD.";
                  if (kpd.key[i].kchar == 'H') {  // 0 = answer
                            if (! ha_fona.pickUp()) 
                            {
                              Serial.println(F("Failed"));
                          } 
                            else 
                            {
                              Serial.println(F("OK!"));
                            iknumber = 0;
                            incall = 1;
                            delay(100);
                            startTime = millis();
                            }                    
                    }                
                  break;
                
                  case RELEASED:
                    msg = " RELEASED.";
                    if (kpd.key[i].kchar=='<')
                    {
                          if (!ha_fona.callPhone(knumber))   //failed to call
                          {
                            iknumber = 0; 
                            startTime = millis();                 
                          } 
                          else                // call success
                          {
                            Serial.print(F("Calling ")); 
                            Serial.println(knumber);
                            iknumber = 0;
                            incall = 1;
                            delay(100);
                            startTime = millis();
                          }
                      
                    }               
                    else if (kpd.key[i].kchar=='>')
                    {
                      if (incall==1)
                      {
                        if (! ha_fona.hangUp()) 
                        {
                          iknumber = 0;
                          flushSerial(); 
                          startTime = millis();                 
                        } 
                        else 
                        {
                          iknumber = 0;                  
                          incall = 0;
                          flushSerial();
                          startTime = millis();
                        }
                      }
                    }                
                    else 
                    {
                        knumber[iknumber] = kpd.key[i].kchar;
                        Serial.println(knumber);
                        Serial.println(iknumber);
                        iknumber = iknumber + 1; 
                        startTime = millis();
                    }                
                  break;
                    case IDLE:
                    msg = " IDLE.";
                }
            }
        }
  }
  
else if ( (millis()-startTime)>3000 ) 
{
        if ( ha_fona.getCallStatus() == 3)
          {
            ha_fona.playTone_LTE(1900, 500, 100, 2);
            //need mapping  fona.setVolume(100);
            delay(1);
            //need mapping  fona.playToolkitTone(3, 1000);
            //need mapping  fona.setVolume(5);
            
          }
   break_to_menu = Serial.read(); //(needs while command above?)
   //Serial.println(command);    
   if ((break_to_menu == 'x')||(break_to_menu == 'X'))
   {
    break;
   }

//  *  smt 12/19/2020:  use ha_fona.getBattDBm(&vbat) to get a value of 5-0, i guess alert when goes down to 2 but need to test
          if (! ha_fona.getBattDBm(&vbat) && (command == 'K')) {
          Serial.println(F("Failed to read Batt"));}
          else if (! ha_fona.getBattDBm(&vbat) && (command = 'k')) {
          Serial.println(F("Failed to read Batt"));}
          else {
            vcc = ha_fona.readVcc();
            //Serial.print("Vcc (V): ");
            //Serial.println(vcc);
 //           Serial.println(String((float)vcc/1000.0, 3));
            startTime = millis();
//worked!            if (vcc < 3307) {ha_fona.playTone_LTE(1500, 250, 100, 2);ha_fona.playTone_LTE(1500, 250, 100, 2);}
            if (vcc < 3000) {ha_fona.playTone_LTE(1500, 250, 100, 2);}
//06/30/21: vcc low beep at 3000 (3.0v ) works but needs to be tested more to make sure everything still functions
          //calls could be made but when low beep starts there is some play between that sound time and button pressing
          //as it stands, once low voltage beeping starts, must crank up past low voltage to soundly use keypad.
          //there should be a way to not have this problem, however, there might also be an issue with 3.0v being too
          //low of a voltage for keypad and other atmega I/O to work, so one could start by making low voltage beep start
          //right below 3300 as vcc stays at roughly 3309 while vbat goes from 3.8 down to 3.3, after 3.3 of vbat, vcc starts to go
          //down too which may start making I/O rocky.  This is why the 3307 value above worked as a low voltage as well.
            
            //else {ha_fona.playTone_LTE(600, 500, 100, 2);}
          /*if  (vbat < 1)     {Serial.print(F("VBat = ")); Serial.print(vbat); Serial.println(F(" dBm integer"));
                                  ha_fona.setGPIO(23,0,1);ha_fona.playTone_LTE(900, 500, 100, 2); 
                                  delay(50);startTime = millis();}
          else if (vbat < 2) {Serial.print(F("VBat = ")); Serial.print(vbat); Serial.println(F(" dBm integer"));
                                  ha_fona.setGPIO(23,0,1);delay(500); ha_fona.setGPIO(23,0,0);startTime = millis();}
          else if (vbat > 3) {;startTime = millis();} 
          else {startTime = millis();}*/
          }
 /* need mapping   
  *  smt 12/19/2020:  use ha_fona.getBattDBm(&vbat) to get a value of 5-0, i guess alert when goes down to 2 but need to test
          if (! fona.getBattVoltage(&vbat) && (command == 'K')) {
          Serial.println(F("Failed to read Batt"));}
          else if (! fona.getBattVoltage(&vbat) && (command = 'k')) {
          Serial.println(F("Failed to read Batt"));}
          else {
          if  (vbat < 3500)     {Serial.print(F("VBat = ")); Serial.print(vbat); Serial.println(F(" mV"));
                                  digitalWrite(13, HIGH);fona.setVolume(80); delay(50); fona.playToolkitTone(6, 500); 
                                  delay(50);fona.setVolume(5);startTime = millis();}
          else if (vbat < 3600) {Serial.print(F("VBat = ")); Serial.print(vbat); Serial.println(F(" mV"));
                                  digitalWrite(13, HIGH);delay(500); digitalWrite(13, LOW);startTime = millis();}
          else if (vbat > 3900) {digitalWrite(13, HIGH);startTime = millis();} 
          else {digitalWrite(13, LOW); startTime = millis();}
          }
end need mapping */
}



}
    break;
}
    /*** test pins ***/

    case 'g': {
        flushSerial();
        Serial.print(F("Type a pin number to test, setting high "));
        uint8_t pin_num = readnumber();
//        char pin_num = readnumber();
        Serial.println();
        pinMode(pin_num, OUTPUT);
        digitalWrite(pin_num, HIGH);
        Serial.print(F("Set this pin high = ")); Serial.print(pin_num); Serial.println(F("%"));

        break;
      }
/*    case 'r': {
        flushSerial();
        Serial.print(F("read a bunch of pins "));
 //       uint8_t pin_num = readnumber();

        //digitalRead(5);
        Serial.print(digitalRead(5));Serial.println(F("  \n"));
        Serial.print(digitalRead(10));Serial.println(F("  \n"));
        Serial.print(digitalRead(18));Serial.println(F("  \n"));
        Serial.print(digitalRead(19));Serial.println(F("  \n"));
        Serial.print(digitalRead(20));Serial.println(F("  \n"));
        Serial.print(digitalRead(21));Serial.println(F("  \n"));
        Serial.print(digitalRead(22));Serial.println(F("  \n"));
        Serial.print(digitalRead(23));Serial.println(F("  \n"));


        break;
      }
*/
    case 'H': {
        flushSerial();
        Serial.print(F("Type a pin number to test, setting high "));
//        uint8_t pin_num = readnumber();
//        char pin_num = readnumber();
//        Serial.println();
        pinMode(A5, OUTPUT);
        digitalWrite(A5, HIGH);
        Serial.print(F("Set this pin high = ")); Serial.print("A5"); Serial.println(F("%"));

        break;
      }
    case 'l': {
        flushSerial();
        Serial.print(F("Type a pin number to test, setting low "));
        uint8_t pin_num = readnumber();
//        char pin_num = readnumber();
        Serial.println();
        pinMode(pin_num, OUTPUT);
        digitalWrite(pin_num, LOW);
        Serial.print(F("Set this pin low = ")); Serial.print(pin_num); Serial.println(F("%"));
        break;
      }
    case 't': {
        flushSerial();
        Serial.print(F("Type a pin number to test, toggle low 1 sec "));
        uint8_t pin_num = readnumber();
        Serial.println();
        pinMode(pin_num, OUTPUT);
        digitalWrite(pin_num, LOW);
        digitalWrite(pin_num, HIGH); delay(1000); digitalWrite(pin_num, LOW);
        Serial.print(F("Toggled this pin low for 1 second = ")); Serial.print(pin_num); Serial.println(F("%"));
        break;
      }
    case 'T': {
        flushSerial();
        Serial.print(F("Type a pin number to test, toggle low 200 millis "));
        uint8_t pin_num = readnumber();
        Serial.println();
        pinMode(pin_num, OUTPUT);
        digitalWrite(pin_num, HIGH);
        Serial.print(F("Toggled this pin low for 200 millis = ")); Serial.print(pin_num); Serial.println(F("%"));
        break;
      }
      
 
 
    default: {
      Serial.println(F("Unknown command"));
      //printMenu();
      break;
    }
  }
  
  // flush input
  flushSerial();
}

void flushSerial() {
    while (Serial.available()) 
    Serial.read();
}

char readBlocking() {
  while (!Serial.available());
  return Serial.read();
}
uint16_t readnumber() {
  uint16_t x = 0;
  char c;
  while (! isdigit(c = readBlocking())) {
    //Serial.print(c);
  }
  Serial.print(c);
  x = c - '0';
  while (isdigit(c = readBlocking())) {
    Serial.print(c);
    x *= 10;
    x += c - '0';
  }
  return x;
}
  
uint8_t readline(char *buff, uint8_t maxbuff, uint16_t timeout) {
  uint16_t buffidx = 0;
  boolean timeoutvalid = true;
  if (timeout == 0) timeoutvalid = false;
  
  while (true) {
    if (buffidx > maxbuff) {
      //Serial.println(F("SPACE"));
      break;
    }

    while(Serial.available()) {
      char c =  Serial.read();

      //Serial.print(c, HEX); Serial.print("#"); Serial.println(c);

      if (c == '\r') continue;
      if (c == 0xA) {
        if (buffidx == 0)   // the first 0x0A is ignored
          continue;
        
        timeout = 0;         // the second 0x0A is the end of the line
        timeoutvalid = true;
        break;
      }
      buff[buffidx] = c;
      buffidx++;
    }
    
    if (timeoutvalid && timeout == 0) {
      //Serial.println(F("TIMEOUT"));
      break;
    }
    delay(1);
  }
  buff[buffidx] = 0;  // null term
  return buffidx;
}
