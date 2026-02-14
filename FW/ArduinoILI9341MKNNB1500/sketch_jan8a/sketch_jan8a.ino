// Most accurate way is to use 3 points to calibrate position and misalignment
// However the math is quite cumbersome so will be using 2 points.
// Use a library for more accuracy and 3-point calibration.

#include <SPI.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ILI9341.h>
#include <XPT2046_Touchscreen.h>
#include <math.h>

// ---------- Pins ----------
#define TFT_DC   6
#define TFT_CS   7
#define TFT_RST  -1

#define TOUCH_CS 5

#define ROTATION 1

// ---------- Display / Touch ----------
Adafruit_ILI9341 tft(TFT_CS, TFT_DC, TFT_RST);
XPT2046_Touchscreen ts(TOUCH_CS);  // no IRQ

// Calibration model: screen = raw * M + C
float xCalM = 0.0f, yCalM = 0.0f; // slope
float xCalC = 0.0f, yCalC = 0.0f; // intercept

int16_t blockWidth  = 20;
int16_t blockHeight = 20;
int16_t blockX = 0, blockY = 0;   // block position in pixels

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

unsigned long lastFrame = 0;

void moveBlock() {
  int16_t newBlockX = blockX;
  int16_t newBlockY = blockY;

  if (ts.touched()) {
    TS_Point p = ts.getPoint();
    ScreenPoint sp = getScreenCoords(p.x, p.y);

    newBlockX = sp.x - (blockWidth / 2);
    newBlockY = sp.y - (blockHeight / 2);

    if (newBlockX < 0) newBlockX = 0;
    if (newBlockX > (int16_t)(tft.width() - blockWidth))  newBlockX = tft.width() - blockWidth;

    if (newBlockY < 0) newBlockY = 0;
    if (newBlockY > (int16_t)(tft.height() - blockHeight)) newBlockY = tft.height() - blockHeight;
  }

  if ((abs(newBlockX - blockX) > 2) || (abs(newBlockY - blockY) > 2)) {
    tft.fillRect(blockX, blockY, blockWidth, blockHeight, ILI9341_BLACK);
    blockX = newBlockX;
    blockY = newBlockY;
    tft.fillRect(blockX, blockY, blockWidth, blockHeight, ILI9341_RED);
  }
}

void setup() {
  Serial.begin(9600);

  pinMode(TFT_CS, OUTPUT);
  pinMode(TOUCH_CS, OUTPUT);
  digitalWrite(TFT_CS, HIGH);
  digitalWrite(TOUCH_CS, HIGH);

  tft.begin();
  tft.setRotation(ROTATION);
  tft.fillScreen(ILI9341_BLACK);

  ts.begin();
  ts.setRotation(ROTATION);

  calibrateTouchScreen();

  // draw initial block
  tft.fillRect(blockX, blockY, blockWidth, blockHeight, ILI9341_RED);

  lastFrame = millis();
}

void loop() {
  // limit frame rate to ~50 FPS (20ms)
  while ((millis() - lastFrame) < 20) { /* wait */ }
  lastFrame = millis();

  moveBlock();
    TS_Point p;
    ScreenPoint sp;
    if (ts.touched()) {
    TS_Point p = ts.getPoint();
    sp = getScreenCoords(p.x, p.y);
    Serial.println(sp.x);Serial.println(sp.y);
}
}
