#include "keyboard.h"
#include "buttons.h"
#include "types.h"
#include <Adafruit_ILI9341.h>
#include "UI.h"

extern Adafruit_ILI9341 tft;

// ---------- Portrait layout (240 x 320) ----------
static const int HEADER_H = 70;
static const int KB_X     = 2;
static const int KB_Y     = HEADER_H + 8;

static const int KEY_W    = 21;
static const int KEY_H    = 56;
static const int KEY_GAP  = 3;   // slightly wider gap = clearer boundaries

static const int SHIFT_W  = 32;
static const int BKSP_W   = 32;
static const int MODE_W   = 44;
static const int SEND_W   = 88;

static const int NUM_W    = 77;
static const int NUM_H    = 52;
static const int NUM_GAP  = 3;

static const int MAX_TEXT = 160;

// ── iOS-style color palette ───────────────────────────────────────
// These are initialized in drawKeyboard/drawNumberPad after tft is ready
static uint16_t COL_KEY;       // regular key: dark gray
static uint16_t COL_SPECIAL;   // shift, BK, 123, ABC: slightly lighter gray
static uint16_t COL_SEND;      // iOS blue
static uint16_t COL_KB_BG;     // keyboard background: near black
static uint16_t COL_FIELD;     // input field: white
static uint16_t COL_HEADER;    // header bar background

static bool colorsInit = false;

static void initColors() {
  if (colorsInit) return;
  COL_KEY     = tft.color565(50,  50,  50);
  COL_SPECIAL = tft.color565(80,  80,  80);
  COL_SEND    = tft.color565(0,  122, 255);
  COL_KB_BG   = tft.color565(22,  22,  22);
  COL_FIELD   = tft.color565(255, 255, 255);
  COL_HEADER  = tft.color565(28,  28,  30);
  colorsInit  = true;
}

static Button kbKeys[31];
static Button numKeys[12];
static Button kbBackBtn;
static Button toBtn;
static Button msgBtn;
static Button switchkeyboardButton;

static bool kbDrawn     = false;
static bool msgOrNumber = false;
static bool alphaMode   = false;
static bool capsLock    = false;

static char typed[MAX_TEXT];
static int  typedLen = 0;

// ---------- Helpers ----------

static void drawCaret(int startX, int baselineY) {
  int16_t x1, y1;
  uint16_t w, h;
  tft.setFont(NULL);
  tft.setTextSize(2);
  if (typedLen == 0) { w = 0; }
  else tft.getTextBounds(typed, startX, baselineY, &x1, &y1, &w, &h);
  tft.drawFastVLine(startX + w + 2, baselineY - 2, 20, COL_SEND);
}

static void clearTypedBuffer() {
  typedLen = 0;
  typed[0] = '\0';
}

static void drawTypedLine() {
  tft.fillRect(76, 0, tft.width() - 76, 34, COL_FIELD);
  tft.fillRect(0, 44, tft.width(), 34, COL_FIELD);
  tft.setCursor(4, 46);
  uiUseDefaultFont();
  tft.setTextColor(ILI9341_BLACK);
  tft.print(typed);
  drawCaret(4, 46);
}

static void drawTypedLineNum() {
  tft.fillRect(76, 0, tft.width() - 76, 34, COL_FIELD);
  tft.fillRect(0, 44, tft.width(), 34, COL_FIELD);
  tft.setCursor(80, 8);
  uiUseDefaultFont();
  tft.setTextColor(ILI9341_BLACK);
  tft.print(typed);
  drawCaret(80, 8);
}

static void redrawTypedArea() {
  if (msgOrNumber) drawTypedLine();
  else             drawTypedLineNum();
}

static void appendChar(char c) {
  if (typedLen >= MAX_TEXT - 1) return;
  typed[typedLen++] = c;
  typed[typedLen]   = '\0';
  Serial.print("KEY: "); Serial.println(c);
  redrawTypedArea();
}

static void backspaceChar() {
  if (typedLen <= 0) return;
  typed[--typedLen] = '\0';
  Serial.println("KEY: <BKSP>");
  redrawTypedArea();
}

void keyboardClearText(void) {
  clearTypedBuffer();
  msgOrNumber = true;
  redrawTypedArea();
}

// ---------- Header ----------

static void drawHeader() {
  initColors();

  // Full screen background
  tft.fillScreen(COL_KB_BG);

  // Header bar
  tft.fillRect(0, 0, tft.width(), HEADER_H - 4, COL_HEADER);

  // Input fields (white boxes)
  tft.fillRect(76, 2,  tft.width() - 78, 30, COL_FIELD);
  tft.fillRect(0,  38, tft.width(),       28, COL_FIELD);

  // Labels
  tft.setTextColor(ILI9341_WHITE);
  tft.setTextSize(2);
  tft.setCursor(34, 6);
  tft.print("To:");
  tft.setCursor(6, 42);
  tft.print("Msg:");

  // Buttons on header
  kbBackBtn.initButton(2, 2, 28, 28, "<", COL_SPECIAL);
  toBtn.initButton(76, 2, tft.width() - 78, 30, "", COL_FIELD);
  msgBtn.initButton(0, 38, tft.width(), 28, "", COL_FIELD);
  switchkeyboardButton.initButton(tft.width() - 50, 38, 48, 24, alphaMode ? "123" : "ABC", COL_SPECIAL);

  redrawTypedArea();
}

// ---------- 3x4 Numpad ----------

static void drawNumberPad() {
  drawHeader();
  initColors();

  const char* labels[12] = {
    "1","2","3",
    "4","5","6",
    "7","8","9",
    "ABC","0","BK"
  };

  for (int row = 0; row < 4; row++) {
    for (int col = 0; col < 3; col++) {
      int i   = row * 3 + col;
      int x   = KB_X + col * (NUM_W + NUM_GAP);
      int y   = KB_Y + row * (NUM_H + NUM_GAP);

      // Bottom row: ABC and BK are special, 0 is regular
      uint16_t c = (i == 9 || i == 11) ? COL_SPECIAL : COL_KEY;
      numKeys[i].initButton(x, y, NUM_W, NUM_H, labels[i], c);
    }
  }

  kbDrawn = true;
}

// ---------- Alpha keyboard ----------

static void drawKeyboard() {
  drawHeader();
  initColors();
  int idx = 0;

  const char* r1 = capsLock ? "QWERTYUIOP" : "qwertyuiop";
  const char* r2 = capsLock ? "ASDFGHJKL"  : "asdfghjkl";
  const char* r3 = capsLock ? "ZXCVBNM"    : "zxcvbnm";

  // Row 1: 10 regular keys
  for (int i = 0; i < 10; i++) {
    char label[2] = { r1[i], '\0' };
    kbKeys[idx++].initButton(KB_X + i * (KEY_W + KEY_GAP), KB_Y, KEY_W, KEY_H, label, COL_KEY);
  }

  // Row 2: 9 regular keys, centred
  int r2off = KB_X + (KEY_W + KEY_GAP) / 2 + 2;
  for (int i = 0; i < 9; i++) {
    char label[2] = { r2[i], '\0' };
    kbKeys[idx++].initButton(r2off + i * (KEY_W + KEY_GAP), KB_Y + KEY_H + KEY_GAP, KEY_W, KEY_H, label, COL_KEY);
  }

  // Row 3: special ^ + 7 letters + special BK
  int y3 = KB_Y + 2 * (KEY_H + KEY_GAP);
  kbKeys[idx++].initButton(KB_X, y3, SHIFT_W, KEY_H, capsLock ? "CAP" : "^", COL_SPECIAL); // 19

  int lettersStart = KB_X + SHIFT_W + KEY_GAP;
  for (int i = 0; i < 7; i++) {
    char label[2] = { r3[i], '\0' };
    kbKeys[idx++].initButton(lettersStart + i * (KEY_W + KEY_GAP), y3, KEY_W, KEY_H, label, COL_KEY); // 20-26
  }
  kbKeys[idx++].initButton(tft.width() - BKSP_W - KB_X, y3, BKSP_W, KEY_H, "BK", COL_SPECIAL); // 27

  // Row 4: special 123 | regular SPC | accent SEND
  int y4   = KB_Y + 3 * (KEY_H + KEY_GAP);
  int spcW = tft.width() - KB_X*2 - MODE_W - SEND_W - KEY_GAP*2;
  kbKeys[idx++].initButton(KB_X,                        y4, MODE_W, KEY_H, "123",  COL_SPECIAL); // 28
  kbKeys[idx++].initButton(KB_X + MODE_W + KEY_GAP,     y4, spcW,   KEY_H, "SPC",  COL_KEY);     // 29
  kbKeys[idx++].initButton(tft.width() - SEND_W - KB_X, y4, SEND_W, KEY_H, "SEND", COL_SEND);    // 30

  kbDrawn = true;
}

// ---------- Button helpers ----------

bool keyboardBackPressed(const ScreenPoint& sp) { return kbBackBtn.isClicked(sp); }
bool msgBtnPressed(const ScreenPoint& sp)        { return msgBtn.isClicked(sp); }
bool toBtnPressed(const ScreenPoint& sp)         { return toBtn.isClicked(sp); }

// ---------- Public tick ----------

bool keyboardTick(const ScreenPoint& sp, bool justTouched) {
  if (!kbDrawn) {
    if (alphaMode) drawKeyboard();
    else           drawNumberPad();
  }

  if (!justTouched) return false;

  Serial.print("x="); Serial.print(sp.x);
  Serial.print(" y="); Serial.println(sp.y);

  if (switchkeyboardButton.isClicked(sp)) {
    alphaMode = !alphaMode;
    kbDrawn   = false;
    if (alphaMode) drawKeyboard(); else drawNumberPad();
    return false;
  }

  // -------- NUMPAD MODE --------
  if (!alphaMode) {
    for (int i = 0; i < 12; i++) {
      if (!numKeys[i].isClicked(sp)) continue;
      if (i == 0)  { appendChar('1'); return false; }
      if (i == 1)  { appendChar('2'); return false; }
      if (i == 2)  { appendChar('3'); return false; }
      if (i == 3)  { appendChar('4'); return false; }
      if (i == 4)  { appendChar('5'); return false; }
      if (i == 5)  { appendChar('6'); return false; }
      if (i == 6)  { appendChar('7'); return false; }
      if (i == 7)  { appendChar('8'); return false; }
      if (i == 8)  { appendChar('9'); return false; }
      if (i == 9)  { alphaMode = true; kbDrawn = false; drawKeyboard(); return false; }
      if (i == 10) { appendChar('0'); return false; }
      if (i == 11) { backspaceChar(); return false; }
    }
    return false;
  }

  // -------- ALPHA MODE --------
  for (int i = 0; i < 31; i++) {
    if (!kbKeys[i].isClicked(sp)) continue;

    const char* r1 = capsLock ? "QWERTYUIOP" : "qwertyuiop";
    const char* r2 = capsLock ? "ASDFGHJKL"  : "asdfghjkl";
    const char* r3 = capsLock ? "ZXCVBNM"    : "zxcvbnm";

    if (i >= 0  && i <= 9)  { appendChar(r1[i]);      return false; }
    if (i >= 10 && i <= 18) { appendChar(r2[i - 10]); return false; }
    if (i == 19) { capsLock = !capsLock; kbDrawn = false; drawKeyboard(); return false; } 
    if (i >= 20 && i <= 26) { appendChar(r3[i - 20]); return false; }
    if (i == 27) { backspaceChar(); return false; }
    if (i == 28) { alphaMode = false; kbDrawn = false; drawNumberPad(); return false; }
    if (i == 29) { appendChar(' '); return false; }
    if (i == 30) {
      Serial.print(msgOrNumber ? "MSG: " : "PHONE: ");
      Serial.println(typed);
      return true;
    }
  }
  return false;
}

// ---------- Getters / reset ----------

const char* keyboardGetText(void) { return typed; }

void keyboardReset(void) {
  kbDrawn     = false;
  typedLen    = 0;
  typed[0]    = '\0';
  msgOrNumber = false;
  alphaMode   = false;
  capsLock    = false;
}