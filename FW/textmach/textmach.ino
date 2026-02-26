/*




*/
#include "Adafruit_GFX.h" // -> 
#include "Adafruit_ILI9341.h" // -> Dislpay chip lib
#include "XPT2046_Touchscreen.h" // -> Touch chip lib 
#include "SPI.h"
#include "Math.h"
#include "keyboard.h"

#include "types.h"

#include "buttons.h"

#include "messages.h"


#define TFT_DC   6
#define TFT_CS   7
#define TFT_RST -1

#define TS_CS 5

// Derived from calibration.ino


// Calibrated using calibrateScreen, only need to do once. 
// Calibration model: screen = raw * M + C
float xCalM = -0.063755f, yCalM = -0.089485f; // slope
float xCalC = 249.84f, yCalC = 333.47f; // intercept

// Portrait mode calibration values
float xCalM_landscape = -0.089715f, yCalM_landscape = -0.065381f; // slope
float xCalC_landscape =356.25f, yCalC_landscape = 246.22; // intercept 
bool menuDrawn = false;
bool numberAquired = false;

int displayConvo = 0;


#define SD_CS 0


#define ROTATION 2

// Use hardware SPI (on Uno, #13, #12, #11) and the above for CS/DC/RST
Adafruit_ILI9341 tft = Adafruit_ILI9341(TFT_CS, TFT_DC, TFT_RST);
XPT2046_Touchscreen ts(TS_CS);

#define MAX_PHONE_LEN 20      // includes null
#define MAX_BODY_LEN 161      // 160 + null
#define MAX_BODY_RECEIVE_LEN 300 // Max amount of a received message 

static char recipientNumber[MAX_PHONE_LEN];
static char msgBody[MAX_BODY_LEN];

#include <MKRNB.h> // -> Modem lib
#include <stdio.h>
//Initialize library instance
NB nbAccess;
NB_SMS sms;

/*
struct ScreenPoint {
  int16_t x;
  int16_t y;
  ScreenPoint(int16_t xIn = 0, int16_t yIn = 0) : x(xIn), y(yIn) {}
};
*/

ScreenPoint getScreenCoords(int16_t rawX, int16_t rawY) {
  // screen = m*raw + c, round to nearest pixel
  int16_t xCoord = (int16_t)lroundf((rawX * xCalM) + xCalC);
  int16_t yCoord = (int16_t)lroundf((rawY * yCalM) + yCalC);

  // clamp to screen
  if (xCoord < 0) xCoord = 0;
  if (xCoord >= (int16_t)tft.width())  xCoord = tft.width() - 1;
  if (yCoord < 0) yCoord = 0;
  if (yCoord >= (int16_t)tft.height()) yCoord = tft.height() - 1;

  return ScreenPoint(xCoord, yCoord);
}

ScreenPoint getScreenCoordsLandscape(int16_t rawX, int16_t rawY) {
  // screen = m*raw + c, round to nearest pixel
  int16_t xCoord = (int16_t)lroundf((rawX * xCalM_landscape) + xCalC_landscape);
  int16_t yCoord = (int16_t)lroundf((rawY * yCalM_landscape) + yCalC_landscape);
  
  // clamp to screen
  if (xCoord < 0) xCoord = 0;
  if (xCoord >= (int16_t)tft.width())  xCoord = tft.width() - 1;
  if (yCoord < 0) yCoord = 0;
  if (yCoord >= (int16_t)tft.height()) yCoord = tft.height() - 1;

  return ScreenPoint(xCoord, yCoord);
}




void setup() {
  Serial.begin(9600);
  while(!Serial); // Wait for serial port to connect

  pinMode(TFT_CS, OUTPUT);
  pinMode(TS_CS, OUTPUT);
  digitalWrite(TFT_CS, HIGH);
  digitalWrite(TS_CS, HIGH);

  tft.begin();
  tft.setRotation(ROTATION);
  tft.fillScreen(ILI9341_BLACK);

  ts.begin();
  ts.setRotation(ROTATION);

  // calibrateTouchScreen();

    // connection state
  bool connected = false;
  // Start NB module
  // If SIM has PIN, pass it as a parameter of begin() in quotes
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
  // calibrateTouchScreen();

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

void receive() {
  int c;
  char senderNumber[20];
  char senderBody[300];
  int i = 0;

  // If there are any SMSs available()
  if (sms.available()) {
    Serial.println("Message received from:");
    tft.println("-----------------");
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
    // Make space for the message. 
    tft.println();

    tft.println("-----------------");


    // Delete message from modem memory
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
  //Serial.println("Message:");
  //Serial.println(message);
  //Serial.println("To:");
  //Serial.println(remoteNum);
  pushMessage(remoteNum, message, OUT);

}// What to assign char* to -> can it not be a fixed arrray?
/*

Will have its own I/F when message is sending / sent 
Redirect to messages state when done 

*/
  

// Complete ordered list of all states that screen will take. 
enum UiState {
  UI_MENU,
  UI_MESSAGES,
  UI_REFRESH,
  UI_COMPOSE,
  UI_CONVO
};

UiState currentState = UI_MENU;

void loop() {
  // Limit frame rate to 50FPS
 //  while ((millis() - lastFrame) < 20) { /* wait */ }
  // lastFrame = millis();
  // Draw once and then wait for touch

  static Button msgBtn;
  static Button compBtn;
  static Button refreshBtn;
  static Button contactsBtn;
  static Button backBtn;

  int c;

  int i = 0;

  static bool isDrawnConvo = false;

  // Initial menu draw
  if(!menuDrawn){
  tft.setTextColor(ILI9341_WHITE);
  tft.setTextSize(2);
  tft.fillScreen(ILI9341_BLACK);
  tft.setRotation(ROTATION);
  ts.setRotation(ROTATION);

  msgBtn.initButton(0, 60, 240, 40, "Messages");
  compBtn.initButton(0,110,240,40, "Compose");
  refreshBtn.initButton(0,160,240,40, "Refresh");
  contactsBtn.initButton(0,210,240,40, "Contacts");
 
  menuDrawn = true;
  }   


    bool touched = false;
    ScreenPoint sp;
    ScreenPoint spLandscape;


    // Touch Logic / UI FSM\
    // read touch gate click checks.. 
  if (ts.touched()) {
    delay(5); // wait for internals to settle down before measuring 
    if (ts.touched()) { // Now measure -> Prevent 0,0 garbage
        TS_Point p = ts.getPoint();
        // if (p.x > 0 && p.y > 0) {
            touched = true;
            sp = getScreenCoords(p.x, p.y);
            spLandscape = getScreenCoordsLandscape(p.x, p.y);
        // }
    }
  }
    // Single centralized debounce that is passed into different display / touch frameworks 
  static bool wasTouched = false;
  bool justPressed = false;
  if (touched && !wasTouched) justPressed = true;
  wasTouched = touched;

    /*Serial.println(sp.x);
    Serial.println(sp.y); Debug coordinates, right now touch is ok. */

    // FSM
    switch(currentState){

    case UI_MENU: {

        if (ts.touched() && msgBtn.isClicked(sp)) {
        // Display recents chronologically and 
        while (ts.touched()) delay(10); // Wait for the transition between screens
        currentState = UI_MESSAGES;
        return; // Dont bother checking other conditions
      }

      if (ts.touched() && compBtn.isClicked(sp)) {
        ts.setRotation(1); // landscape mode
        currentState = UI_COMPOSE;
        //while (ts.touched()) 
        delay(10); // Wait for the transition between screens
        keyboardReset(); // reset
        keyboardTick(spLandscape, justPressed);
        return;
      }
      if (ts.touched() && refreshBtn.isClicked(sp)) {
        tft.fillScreen(ILI9341_BLACK);
        tft.setCursor(0,0);
        receive();


        while (ts.touched()) delay(10); // Wait for the transition between screens
        currentState = UI_MENU;   //  go back to menu
        menuDrawn = false;        // force redraw
        return;
      }

      if (ts.touched() && contactsBtn.isClicked(sp)) {
        tft.fillScreen(ILI9341_ORANGE);
        tft.setCursor(0,0);
        tft.println("Other button touched");
        menuDrawn = false;
        return;
      }
      break;
     }

      case UI_MESSAGES: {
        // Same back btn logic 
        displayConvo = recentMessagesScreen(sp,ts.touched());
        if (ts.touched() && msgBackBtnPressed(sp)) { // Can reuse this function bc same hitbox although created inside of recentMessagesScreen
        currentState = UI_MENU;   
        menuDrawn = false;     
        return;   
        }
        if(displayConvo != -1){
        while (ts.touched()) delay(10); // Wait for the transition between screens
        currentState = UI_CONVO;
        return;
        }

        break;
      }

  case UI_CONVO: {
      if (drawConversationToTFT(sp, ts.touched(), displayConvo, isDrawnConvo)){
        currentState = UI_MESSAGES;
        isDrawnConvo = false;  // Reset for next time
        return;
      }
      isDrawnConvo = true;  // Mark as drawn after first call
      break; 
  }
        
      


      case UI_COMPOSE:     {

        if(!numberAquired){
        if (keyboardBackPressed(spLandscape)) {
        currentState = UI_MENU;   //  go back to menu
        menuDrawn = false;        // force redraw
        return;
        } 
       if (keyboardTick(spLandscape, ts.touched())){
       const char* kb = keyboardGetText();
       strncpy(recipientNumber, kb, MAX_PHONE_LEN - 1);
       recipientNumber[MAX_PHONE_LEN - 1] = '\0';
       numberAquired = true;
       keyboardClearText();
       Serial.print("Phone # aquired");
        }
        }else if(numberAquired){
        if (keyboardBackPressed(spLandscape)) {
        currentState = UI_MENU;   //  go back to menu
        menuDrawn = false;        // force redraw
        return;
        } 
        if (keyboardTick(spLandscape, ts.touched())){
        const char* kb2 = keyboardGetText();
        strncpy(msgBody, kb2, MAX_BODY_LEN - 1);
        msgBody[MAX_BODY_LEN - 1] = '\0';

        // Once keyboard interface has done its job we send it over to the text function
        text(recipientNumber, msgBody);
        Serial.println("TextSent");
        numberAquired = false;
        keyboardReset();
        numberAquired = false;
        return;
        }

        

        // 
        
        break;


    }
    }

}
}


/*
void calibrateTouchScreen() {
  TS_Point p;
  int16_t x1, y1, x2, y2;

  tft.fillScreen(ILI9341_BLACK);

  // wait for no touch
  while (ts.touched()) { delay(10); }

  // first point near top-left (20,20)
  drawCross(20, 20, ILI9341_RED);
  while (!ts.touched()) { delay(10); }
  delay(50);
  p = ts.getPoint();
  x1 = p.x;
  y1 = p.y;
  drawCross(20, 20, ILI9341_BLACK);
  delay(300);
  while (ts.touched()) { delay(10); }

  // second point near bottom-right (width-20, height-20)
  int16_t sx2 = (int16_t)tft.width()  - 20;
  int16_t sy2 = (int16_t)tft.height() - 20;

  drawCross(sx2, sy2, ILI9341_RED);
  while (!ts.touched()) { delay(10); }
  delay(50);
  p = ts.getPoint();
  x2 = p.x;
  y2 = p.y;
  drawCross(sx2, sy2, ILI9341_BLACK);
  delay(300);
  while (ts.touched()) { delay(10); }

  // distances between the two screen targets:
  // (20,20) -> (width-20, height-20)
  int16_t xDist = (int16_t)tft.width()  - 40;
  int16_t yDist = (int16_t)tft.height() - 40;

  // model: screenX = rawX * xCalM + xCalC
  xCalM = (float)xDist / (float)(x2 - x1);
  xCalC = 20.0f - ((float)x1 * xCalM);

  yCalM = (float)yDist / (float)(y2 - y1);
  yCalC = 20.0f - ((float)y1 * yCalM);

  Serial.println("Calibration:");
  Serial.print("raw1: "); Serial.print(x1); Serial.print(", "); Serial.println(y1);
  Serial.print("raw2: "); Serial.print(x2); Serial.print(", "); Serial.println(y2);
  Serial.print("xCalM="); Serial.print(xCalM, 6); Serial.print(" xCalC="); Serial.println(xCalC, 2);
  Serial.print("yCalM="); Serial.print(yCalM, 6); Serial.print(" yCalC="); Serial.println(yCalC, 2);
}

static void drawCross(int16_t cx, int16_t cy, uint16_t color) {
  // simple cross centered at (cx,cy)
  tft.drawFastHLine(cx - 10, cy, 20, color);
  tft.drawFastVLine(cx, cy - 10, 20, color);
}
*/