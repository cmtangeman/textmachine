// Most accurate way is to use 3 points to calibrate position and misalignment 
// However the math is quite cumbersome so will be using 2 points to work out
// Mathematics ourselves. Use package for more accuracy and 3-point calibration. 

#include "SPI.h"
#include "Adafruit_GFX.h" // -> 
#include "Adafruit_ILI9341.h" // -> Dislpay chip lib
#include "XPT2046_Touchscreen.h" // -> Touch chip lib 
#include "Math.h"


// Put connections here



#define ROTATION 3

// ---------- Display / Touch ----------
Adafruit_ILI9341 tft(TFT_CS, TFT_DC, TFT_RST);
XPT2046_Touchscreen ts(TOUCH_CS);  // no IRQ -> ts is instance of touchscreen object


float xCalM = 0.0, yCalM = 0.0; // slope 
float xCalC = 0.0, yCalC = 0.0 // y intercept

int8_t = blockWidth = 20; 
int8_t = blockHeight = 20;
int16_t blockX = 0, blockY = 0; // block position in pixels

class ScreenPoint {
  public:
    int16_t x;
    int16_t y;

    ScreenPoint(int16_t xIn, int16_t yIn){
      x = xIn;
      y = yIn;
    }
};

ScreenPoint getScreenCoords(int16_t x, int16_t y){
  // mx + C, round to nearest pixel value
  int16_t xCoord = round((x * xCalM)+xCalC);
  int16_t yCoord = round((y * yCalM) + yCalC);
  // Error checking
  // Limit vvalue if something come up off of the edge of the screen
  if(xCoord<0) xCoord = 0;
  if(xCoord >= tft.width()) xCoord = tft.width() -1;
  if(yCoord < 0) yCoord = 0;
  if(yCoord >= tft.height()) yCoord = tft.height() -1;
  return(ScreenPoint(xCoord,yCoord));
}

void calibrateTouchScreen(){
  TS_Point p;
  int16_t x1,y1,x2,y2;

  tft.fillScreen(ILI9341_BLACK);
  // wait for no touch
  while(ts.touched());
  // Once nobody has touched screen draw red cross (for prevention of false readings)
  tft.drawFastHLine(10,20,20,ILI9341_RED);
  tft.drawFastVLine(10,20,20,ILI9341_RED);
  while(!ts.touched());
  delay(50);// As it touches it half touches and gets an erronius value so we wait 50ms for 
  // Firm press against screen. 
  p = ts.getPoint();
  x1 = p.x;
  y1 = p.y;
  tft.drawFastHLine(10,20,20,ILI9341_BLACK);
  tft.drawFastVLine(10,20,20,ILI9341_BLACK); // Over draw in black to take off screen 
  delay(500);
  while(ts.touched()); // Wait for them to take stylus off
  // Draw another red cross centered 20 pixels in and 20 pixels up
  tft.drawFastHLine(tft.width() - 30, tft.height() - 20, 20, ILI9341_RED);
  tft.drawFastVLine(tft.width() - 20, tft.height() - 30, 20, ILI9341_RED);
  while(!ts.touched());
  delay(50);
  p = ts.getPoint();
  x2 = p.x;
  y2 = p.y;
  tft.drawFastHLine(tft.width() - 30, tft.height() - 20, 20, ILI9341_BLACK);
  tft.drawFastVLine(tft.width() - 30, tft.height() - 20, 20, ILI9341_BLACK);


  // Calculation: 

  int16_t xDist = tft.width() - 40;
  int16_t yDist = tft.height() - 40;


  xCalM = (float)xDist/(float)(x2-x1);
  xCalC = 20.0 - ((float)x1*xCalM);
  // y
  yCalM = (float)yDist / (float)(y2-y1);
  yCalC = 20.0 - ((float)y1 * yCalM);
  // Global variables used for calculation
  // X model and y model ton translate raw values coming from T.S into pixel dimensinos

  Serial.print("x1 = ");Serial.print(x1);
  Serial.print(", y1 = ");Serial.print(y1);
}


void setup() {
  // Generalized T.S and LCD Setup
    Serial.begin(9600);

  // Make sure both chip selects are set as high so there are no contention issues
  pinMode(TFT_CS, OUTPUT);
  pinMode(Touch_CS ,OUTPUT);
  digitalWrite(TFT_CS,HIGH);
  digitalWrite(Touch_CS, HIGH);
  

  tft.begin();
  tft.setRotation(ROTATION);
  tft.fillScreen(ILI9341_BLACK);
  ts.begin();
  ts.setRotation(ROTATION);

//
  calibrateTouchScreen();

}

unsinged long lastFrame = millis();

void moveBlock() {
  int16_t newBlockX, newBlockY;
ScreenPoint sp = ScreenPoint(0, 0);

if (ts.touched()) {
    TS_Point p = ts.getPoint();
    sp = getScreenCoords(p.x, p.y);

    newBlockX = sp.x - (blockWidth / 2);
    newBlockY = sp.y - (blockHeight / 2);

    if (newBlockX < 0) newBlockX = 0;
    if (newBlockX >= (tft.width() - blockWidth)) newBlockX = tft.width() - 1 - blockWidth;

    if (newBlockY < 0) newBlockY = 0;
    if (newBlockY >= (tft.height() - blockHeight)) newBlockY = tft.height() - 1 - blockHeight;
}

if ((abs(newBlockX - blockX) > 2) || (abs(newBlockY - blockY) > 2)) {
    tft.fillRect(blockX, blockY, blockWidth, blockHeight, ILI9341_BLACK);
    blockX = newBlockX;
    blockY = newBlockY;
    tft.fillRect(blockX, blockY, blockWidth, blockHeight, ILI9341_RED);
}

}

void loop(void) {
  // limit frame rate 
  while((millis() - lastFrame)< 20);
  lastFrame = millis();


  if(ts.touched()) {
    moveBlock();
  }

}
