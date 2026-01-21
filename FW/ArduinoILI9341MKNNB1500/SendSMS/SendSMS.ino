/*
  MKR NB 1500 + ILI9341 + XPT2046
  Touchscreen "Serial Monitor" GUI for sending SMS

  What it does:
  - Touch UI lets you enter:
      1) Phone number (E.164, e.g. +14155551234)
      2) Message text
  - Tap a simple on-screen keyboard to type
  - Tap SEND to transmit via MKRNB NB_SMS
  - Status shown on screen + also printed to Serial

  Assumptions (match your working tests):
    TFT_DC  = 6
    TFT_CS  = 7
    TOUCH_CS= 5
    TFT_RST tied to MKR RST => TFT_RST = -1
    Shared SPI: MOSI/MISO/SCK on MKR SPI header pins
    Touch IRQ not used

  Libraries (Arduino IDE Library Manager):
    - Adafruit GFX Library
    - Adafruit ILI9341
    - XPT2046_Touchscreen
    - MKRNB (built-in for MKR NB 1500)

  Notes:
  - Touch calibration constants below are placeholders.
    Replace TS_MINX/TS_MAXX/TS_MINY/TS_MAXY with your measured values.
  - This is a "good-enough" GUI keyboard (A-Z, 0-9, +, space, backspace, enter, send, clear).
*/

#include <SPI.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ILI9341.h>
#include <XPT2046_Touchscreen.h>

#include <MKRNB.h>
#include "arduino_secrets.h"

// ---------- Pins ----------
#define TFT_DC   6
#define TFT_CS   7
#define TFT_RST -1

#define TOUCH_CS 5

// ---------- Hardcoded 2-point calibration ----------
const float xCalM = -0.09;
const float xCalC = 329.62;
const float yCalM = -0.06;
const float yCalC = 250.62;


// ---------- SMS ----------
const char PINNUMBER[] = SECRET_PINNUMBER;
NB nbAccess;
NB_SMS sms;

// ---------- Display / Touch ----------
Adafruit_ILI9341 tft(TFT_CS, TFT_DC, TFT_RST);
XPT2046_Touchscreen ts(TOUCH_CS);  // no IRQ

// ---------- UI State ----------
enum InputField { FIELD_NUMBER, FIELD_MESSAGE };
InputField activeField = FIELD_NUMBER;

char phoneBuf[21] = {0};   // 20 chars + null
char msgBuf[201]  = {0};   // 200 chars + null
uint16_t phoneLen = 0;
uint16_t msgLen   = 0;

char statusLine[64] = "Tap Number, Message, then SEND";

static inline int clampi(int v, int lo, int hi) { return (v < lo) ? lo : (v > hi ? hi : v); }

void setStatus(const char* s) {
  strncpy(statusLine, s, sizeof(statusLine) - 1);
  statusLine[sizeof(statusLine) - 1] = '\0';
}

// ---------- Touch mapping using hardcoded calibration ----------
void mapTouchToScreen(const TS_Point& p, int &x, int &y) {
  int xCoord = round(p.x * xCalM + xCalC);
  int yCoord = round(p.y * yCalM + yCalC);

  // Clamp to screen bounds
  x = (xCoord < 0) ? 0 : (xCoord >= tft.width() ? tft.width() - 1 : xCoord);
  y = (yCoord < 0) ? 0 : (yCoord >= tft.height() ? tft.height() - 1 : yCoord);
}


// ---------- Small helpers ----------
void clearBuffers() {
  phoneLen = 0; phoneBuf[0] = '\0';
  msgLen = 0; msgBuf[0] = '\0';
  activeField = FIELD_NUMBER;
}

void appendChar(char c) {
  if (activeField == FIELD_NUMBER) {
    if (phoneLen < 20) {
      phoneBuf[phoneLen++] = c;
      phoneBuf[phoneLen] = '\0';
    }
  } else {
    if (msgLen < 200) {
      msgBuf[msgLen++] = c;
      msgBuf[msgLen] = '\0';
    }
  }
}

void backspaceChar() {
  if (activeField == FIELD_NUMBER) {
    if (phoneLen > 0) phoneBuf[--phoneLen] = '\0';
  } else {
    if (msgLen > 0) msgBuf[--msgLen] = '\0';
  }
}

// ---------- UI drawing ----------
const int HEADER_H = 74;
const int STATUS_H = 18;
const int KEY_H = 40;
const int KEY_W = 48;   // 320/6 ~ 53; using spacing
const int KEY_GAP = 4;

void drawHeader() {
  tft.fillRect(0, 0, tft.width(), HEADER_H, ILI9341_NAVY);

  // Title
  tft.setTextColor(ILI9341_WHITE);
  tft.setTextSize(2);
  tft.setCursor(8, 6);
  tft.print("SMS Sender");

  // Number field box
  int boxW = tft.width() - 16;
  int numY = 30;
  tft.fillRect(8, numY, boxW, 18, (activeField == FIELD_NUMBER) ? ILI9341_DARKGREEN : ILI9341_BLACK);
  tft.drawRect(8, numY, boxW, 18, ILI9341_WHITE);
  tft.setTextSize(1);
  tft.setCursor(10, numY + 5);
  tft.setTextColor(ILI9341_WHITE);
  tft.print("To: ");
  tft.print(phoneBuf);

  // Message field box
  int msgY = 52;
  tft.fillRect(8, msgY, boxW, 18, (activeField == FIELD_MESSAGE) ? ILI9341_DARKGREEN : ILI9341_BLACK);
  tft.drawRect(8, msgY, boxW, 18, ILI9341_WHITE);
  tft.setCursor(10, msgY + 5);
  tft.setTextColor(ILI9341_WHITE);
  tft.print("Msg: ");
  // show last ~28 chars so it fits
  const char* m = msgBuf;
  int mlen = strlen(msgBuf);
  if (mlen > 28) m = msgBuf + (mlen - 28);
  tft.print(m);
}

void drawStatus() {
  int y = HEADER_H;
  tft.fillRect(0, y, tft.width(), STATUS_H, ILI9341_BLACK);
  tft.setTextSize(1);
  tft.setTextColor(ILI9341_YELLOW);
  tft.setCursor(8, y + 5);
  tft.print(statusLine);
}

// Keyboard layout: 6 columns x 4 rows = 24 keys
// Row0: 1 2 3 4 5 6
// Row1: 7 8 9 0 + SPACE
// Row2: A B C D E F
// Row3: G H I J BKSP ENTER
// Bottom bar buttons: CLEAR / SEND (two big buttons)
struct Key {
  const char* label;
  char ch;          // 0 means "action key"
  uint8_t action;   // 0 none, 1 space, 2 backspace, 3 enter
};

Key keys[4][6] = {
  { {"1",'1',0},{"2",'2',0},{"3",'3',0},{"4",'4',0},{"5",'5',0},{"6",'6',0} },
  { {"7",'7',0},{"8",'8',0},{"9",'9',0},{"0",'0',0},{"+",'+',0},{"SP",0,1} },
  { {"A",'A',0},{"B",'B',0},{"C",'C',0},{"D",'D',0},{"E",'E',0},{"F",'F',0} },
  { {"G",'G',0},{"H",'H',0},{"I",'I',0},{"J",'J',0},{"<-",0,2},{"ENT",0,3} }
};

void drawKey(int r, int c, int x, int y, int w, int h) {
  tft.fillRect(x, y, w, h, ILI9341_DARKCYAN);
  tft.drawRect(x, y, w, h, ILI9341_WHITE);
  tft.setTextSize(2);
  tft.setTextColor(ILI9341_WHITE);
  int16_t x1, y1;
  uint16_t tw, th;
  tft.getTextBounds(keys[r][c].label, 0, 0, &x1, &y1, &tw, &th);
  int tx = x + (w - (int)tw) / 2;
  int ty = y + (h - (int)th) / 2;
  tft.setCursor(tx, ty);
  tft.print(keys[r][c].label);
}

void drawKeyboard() {
  int startY = HEADER_H + STATUS_H + 2;

  // Field selectors (tap boxes in header instead, but we also show hint)
  // Keyboard grid
  int totalW = tft.width();
  int colW = (totalW - (KEY_GAP * 7)) / 6;  // 6 keys + 7 gaps
  int rowH = KEY_H;

  for (int r = 0; r < 4; r++) {
    int y = startY + r * (rowH + KEY_GAP);
    for (int c = 0; c < 6; c++) {
      int x = KEY_GAP + c * (colW + KEY_GAP);
      drawKey(r, c, x, y, colW, rowH);
    }
  }

  // Bottom buttons: CLEAR / SEND
  int btnY = startY + 4 * (rowH + KEY_GAP) + 6;
  int btnH = 38;
  int btnW = (tft.width() - 3 * KEY_GAP) / 2;
  int x1 = KEY_GAP;
  int x2 = KEY_GAP * 2 + btnW;

  // CLEAR
  tft.fillRect(x1, btnY, btnW, btnH, ILI9341_MAROON);
  tft.drawRect(x1, btnY, btnW, btnH, ILI9341_WHITE);
  tft.setTextSize(2);
  tft.setTextColor(ILI9341_WHITE);
  tft.setCursor(x1 + 20, btnY + 10);
  tft.print("CLEAR");

  // SEND
  tft.fillRect(x2, btnY, btnW, btnH, ILI9341_DARKGREEN);
  tft.drawRect(x2, btnY, btnW, btnH, ILI9341_WHITE);
  tft.setCursor(x2 + 30, btnY + 10);
  tft.print("SEND");
}

void drawUI() {
  tft.fillScreen(ILI9341_BLACK);
  drawHeader();
  drawStatus();
  drawKeyboard();
}

// ---------- Hit testing ----------
bool hitRect(int x, int y, int rx, int ry, int rw, int rh) {
  return (x >= rx && x < (rx + rw) && y >= ry && y < (ry + rh));
}

void handleTouch(int x, int y) {
  // Tap in header selects field
  int boxW = tft.width() - 16;
  int numY = 30;
  int msgY = 52;

  if (hitRect(x, y, 8, numY, boxW, 18)) {
    activeField = FIELD_NUMBER;
    setStatus("Editing number");
    drawHeader();
    drawStatus();
    return;
  }
  if (hitRect(x, y, 8, msgY, boxW, 18)) {
    activeField = FIELD_MESSAGE;
    setStatus("Editing message");
    drawHeader();
    drawStatus();
    return;
  }

  // Keyboard grid
  int startY = HEADER_H + STATUS_H + 2;
  int colW = (tft.width() - (KEY_GAP * 7)) / 6;
  int rowH = KEY_H;

  for (int r = 0; r < 4; r++) {
    int ky = startY + r * (rowH + KEY_GAP);
    for (int c = 0; c < 6; c++) {
      int kx = KEY_GAP + c * (colW + KEY_GAP);
      if (hitRect(x, y, kx, ky, colW, rowH)) {
        Key &k = keys[r][c];
        if (k.action == 0) {
          appendChar(k.ch);
        } else if (k.action == 1) { // space
          appendChar(' ');
        } else if (k.action == 2) { // backspace
          backspaceChar();
        } else if (k.action == 3) { // enter
          activeField = (activeField == FIELD_NUMBER) ? FIELD_MESSAGE : FIELD_NUMBER;
        }
        drawHeader();
        drawStatus();
        return;
      }
    }
  }

  // Bottom buttons
  int btnY = startY + 4 * (rowH + KEY_GAP) + 6;
  int btnH = 38;
  int btnW = (tft.width() - 3 * KEY_GAP) / 2;
  int x1 = KEY_GAP;
  int x2 = KEY_GAP * 2 + btnW;

  if (hitRect(x, y, x1, btnY, btnW, btnH)) {
    clearBuffers();
    setStatus("Cleared");
    drawUI();
    return;
  }

  if (hitRect(x, y, x2, btnY, btnW, btnH)) {
    // Send SMS
    if (phoneLen < 8) { // minimal sanity check
      setStatus("Enter full number (use +countrycode)");
      drawStatus();
      return;
    }
    if (msgLen < 1) {
      setStatus("Enter message text");
      drawStatus();
      return;
    }
    setStatus("Sending...");
    drawStatus();

    Serial.print("Sending to: "); Serial.println(phoneBuf);
    Serial.print("Msg: "); Serial.println(msgBuf);

    sms.beginSMS(phoneBuf);
    sms.print(msgBuf);
    sms.endSMS();

    setStatus("Sent (check phone). Tap CLEAR for next.");
    drawStatus();
    return;
  }
}

// ---------- Setup / Loop ----------
void setup() {
  Serial.begin(115200);

  // Prevent SPI bus contention at startup
  pinMode(TFT_CS, OUTPUT);
  pinMode(TOUCH_CS, OUTPUT);
  digitalWrite(TFT_CS, HIGH);
  digitalWrite(TOUCH_CS, HIGH);

  SPI.begin();

  tft.begin();
  tft.setRotation(2);

  if (!ts.begin()) {
    tft.fillScreen(ILI9341_BLACK);
    tft.setTextColor(ILI9341_RED);
    tft.setTextSize(2);
    tft.setCursor(10, 10);
    tft.println("Touch init FAIL");
    while (1) {}
  }
  ts.setRotation(2);

  drawUI();

  setStatus("Initializing cellular...");
  drawStatus();

  bool connected = false;
  while (!connected) {
    if (nbAccess.begin(PINNUMBER) == NB_READY) {
      connected = true;
    } else {
      setStatus("Not connected (retrying)...");
      drawStatus();
      delay(1000);
    }
  }

  setStatus("NB ready. Tap Number/Msg, then SEND.");
  drawStatus();
}

void loop() {
  if (ts.touched()) {
    TS_Point p = ts.getPoint();
    int x, y;
    mapTouchToScreen(p, x, y);
    handleTouch(x, y);

    // basic debounce
    delay(140);
    while (ts.touched()) delay(20);
  }
}
