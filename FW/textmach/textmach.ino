/*




*/
#include "Adafruit_GFX.h" // -> 
#include "Adafruit_ILI9341.h" // -> Dislpay chip lib
#include "XPT2046_Touchscreen.h" // -> Touch chip lib 
#include "SPI.h"
#include "Math.h"


#define TFT_DC   6
#define TFT_CS   7
#define TFT_RST -1

#define TS_CS 5

// Derived from calibration.ino


// Calibrated using calibrateScreen, only need to do once. 
// Calibration model: screen = raw * M + C
float xCalM = -0.063755f, yCalM = -0.089485f; // slope
float xCalC = 249.84f, yCalC = 333.47f; // intercept

bool menuDrawn = false;

#define MINPRESSURE 700


#define SD_CS 0


#define ROTATION 2

// Use hardware SPI (on Uno, #13, #12, #11) and the above for CS/DC/RST
Adafruit_ILI9341 tft = Adafruit_ILI9341(TFT_CS, TFT_DC, TFT_RST);
XPT2046_Touchscreen ts(TS_CS);


#include <MKRNB.h> // -> Modem lib
#include <stdio.h>
//Initialize library instance
NB nbAcess;
NB_SMS sms;


struct ScreenPoint {
  int16_t x;
  int16_t y;
  ScreenPoint(int16_t xIn = 0, int16_t yIn = 0) : x(xIn), y(yIn) {}
};

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

static void drawCross(int16_t cx, int16_t cy, uint16_t color) {
  // simple cross centered at (cx,cy)
  tft.drawFastHLine(cx - 10, cy, 20, color);
  tft.drawFastVLine(cx, cy - 10, 20, color);
}

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

unsigned long lastFrame = 0; // ? 


// 



void setup() {
  Serial.begin(9600);

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

  // draw initial block
  

  // lastFrame = millis();

}
/*
void menuScreen(){
  // Messages button
  TS_Point p;
  ScreenPoint sp;

  
  tft.setTextColor(ILI9341_WHITE);
  tft.setTextSize(2);
  tft.fillScreen(ILI9341_BLACK);
  tft.fillRect(0, 60, 240, 40, ILI9341_BLUE);
  tft.fillRect(0,110,240,40,ILI9341_BLUE);
  tft.setCursor(0+5,80);
  tft.println("Messages");
  tft.setCursor(0+5, 120);
  tft.println("Compose");


    if (ts.touched()) {
    TS_Point p = ts.getPoint();
    sp = getScreenCoords(p.x, p.y);
    Serial.println(sp.x);Serial.println(sp.y);
    
      if (sp.y > 59 && sp.y < 101) {
        tft.fillScreen(ILI9341_BLUE);
        tft.setCursor(0,0);
        tft.setTextColor(ILI9341_WHITE);
        tft.println("Messages touched");
        delay(500);
        return;
      }
      if (sp.y > 110 && sp.y < 150) {
        tft.fillScreen(ILI9341_BLACK);
        tft.setCursor(0,0);
        tft.println("Other button touched");
        delay(500);
        menuDrawn = false;
        return;
      }
  
}
}
*/


void loop() {
  // Limit frame rate to 50FPS
 //  while ((millis() - lastFrame) < 20) { /* wait */ }
  // lastFrame = millis();
  // Draw once and then wait for touch


  if(!menuDrawn){
  tft.setTextColor(ILI9341_WHITE);
  tft.setTextSize(2);
  tft.fillScreen(ILI9341_BLACK);
  tft.fillRect(0, 60, 240, 40, ILI9341_BLUE);
  tft.fillRect(0,110,240,40,ILI9341_BLUE);
  tft.setCursor(0+5,80);
  tft.println("Messages");
  tft.setCursor(0+5, 120);
  tft.println("Compose");
 
    menuDrawn = true;
  }   


    TS_Point p;
    ScreenPoint sp;
    if (ts.touched()) {
    TS_Point p = ts.getPoint();
    sp = getScreenCoords(p.x, p.y);

    Serial.println(sp.x);Serial.println(sp.y);
          if (sp.y > 59 && sp.y < 101) {
        tft.fillScreen(ILI9341_BLUE);
        tft.setCursor(0,0);
        tft.setTextColor(ILI9341_WHITE);
        tft.println("Messages touched");
        delay(500);
        menuDrawn = false;
        return;
      }
      if (sp.y > 110 && sp.y < 150) {
        tft.fillScreen(ILI9341_BLACK);
        tft.setCursor(0,0);
        tft.println("Other button touched");
        delay(500);
        menuDrawn = false;
        return;
      }
}

}
