#include "keyboard.h"
#include "buttons.h"
#include "types.h"
#include <Adafruit_ILI9341.h>
#include <XPT2046_Touchscreen.h>
#include "UI.h"

extern Adafruit_ILI9341 tft;
extern XPT2046_Touchscreen ts;

// ---------- Layout ----------
static const int HEADER_H = 70;

static const int KB_X = 4;
static const int KB_Y = HEADER_H + 8;

static const int KEY_W   = 29;
static const int KEY_H   = 38;
static const int KEY_GAP = 2;

static const int SHIFT_W = 42;
static const int BKSP_W  = 42;
static const int MODE_W  = 70;
static const int SPACE_W = 156;
static const int SEND_W  = 86;

static const int MAX_TEXT = 160;

// Alpha keys: 10 + 9 + 9 + 3 = 31
static Button kbKeys[31];

// Number keys: 10 + 10 + 7 + 3 = 30
static Button numKeys[30];

static Button kbBackBtn;
static Button toBtn;
static Button msgBtn;
static Button switchkeyboardButton;

static bool kbDrawn = false;

// false = entering phone number
// true  = entering message
static bool msgOrNumber = false;

// false = symbols/numbers keyboard
// true  = alphabet keyboard
static bool alphaMode = false;


static bool capsLock = false;   // default lowercase

static char typed[MAX_TEXT];
static int typedLen = 0;

// ----------------- Helpers -----------------

static void drawCaret(int startX, int baselineY) {
  int16_t x1, y1;
  uint16_t w, h;

  tft.setFont(NULL);      // use default font since your typed text is default font
  tft.setTextSize(2);

  if (typedLen == 0) {
    w = 0;
    h = 16;
  } else {
    tft.getTextBounds(typed, startX, baselineY, &x1, &y1, &w, &h);
  }

  int caretX = startX + w + 2;
  int caretY = baselineY - 2;
  int caretH = 20;

  tft.drawFastVLine(caretX, caretY, caretH, ILI9341_BLUE);
}

static void clearTypedBuffer() {
  typedLen = 0;
  typed[0] = '\0';
}

static void drawTypedLine() {
  // Top "To" field inactive
  tft.fillRect(76, 0, tft.width() - 76, 34, ILI9341_WHITE);

  // Bottom message field active
  tft.fillRect(0, 44, tft.width(), 34, ILI9341_WHITE);

  
  tft.setCursor(4, 46);
  uiUseButtonFont();
  tft.setTextColor(ILI9341_BLACK);
  tft.setTextColor(ILI9341_BLACK);
  tft.print(typed);

  drawCaret(4, 46);
}

static void drawTypedLineNum() {
  // Top number field active
  tft.fillRect(76, 0, tft.width() - 76, 34, ILI9341_WHITE);

  // Bottom msg field inactive
  tft.fillRect(0, 44, tft.width(), 34, ILI9341_WHITE);

  
  tft.setCursor(80, 8);
  uiUseButtonFont();
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
  typed[typedLen] = '\0';

  Serial.print("KEY: ");
  Serial.println(c);

  redrawTypedArea();
}

static void backspaceChar() {
  if (typedLen <= 0) return;

  typedLen--;
  typed[typedLen] = '\0';

  Serial.println("KEY: <BKSP>");
  redrawTypedArea();
}

void keyboardClearText(void) {
  clearTypedBuffer();
  msgOrNumber = true;
  redrawTypedArea();
}

// ----------------- Drawing top/header -----------------

static void drawHeader() {
  tft.fillScreen(ILI9341_BLACK);
  tft.setTextColor(ILI9341_WHITE);
  tft.setTextSize(2);

  kbBackBtn.initButton(0, 0, 30, 30, "<");

  // Top fields
  msgBtn.initButton(0, 44, tft.width(), 34, "");
  toBtn.initButton(76, 0, tft.width() - 76, 34, "");

  // Top-right toggle button
  switchkeyboardButton.initButton(250, 6, 64, 28, alphaMode ? "123" : "ABC");

  tft.setCursor(46, 6);
  tft.print("To:");

  tft.setCursor(6, 52);
  tft.print("Msg:");

  redrawTypedArea();
}

// ----------------- Number / symbols keyboard -----------------

static void drawNumberPad() {
  tft.setRotation(1);
  ts.setRotation(1);

  drawHeader();

  int idx = 0;

  // Row 1: 1 2 3 4 5 6 7 8 9 0
  const char* r1 = "1234567890";
  for (int i = 0; i < 10; i++) {
    int x = KB_X + i * (KEY_W + KEY_GAP);
    int y = KB_Y;
    char label[2] = { r1[i], '\0' };
    numKeys[idx++].initButton(x, y, KEY_W, KEY_H, label);
  }

  // Row 2: - / : ; ( ) $ & @ "
  const char* r2Labels[10] = { "-", "/", ":", ";", "(", ")", "$", "&", "@", "\"" };
  for (int i = 0; i < 10; i++) {
    int x = KB_X + i * (KEY_W + KEY_GAP);
    int y = KB_Y + (KEY_H + KEY_GAP);
    numKeys[idx++].initButton(x, y, KEY_W, KEY_H, r2Labels[i]);
  }

  // Row 3: #+= . , ? ! ' BK
  int y3 = KB_Y + 2 * (KEY_H + KEY_GAP);
  numKeys[idx++].initButton(KB_X + 0,   y3, 50, KEY_H, "#+=");
  numKeys[idx++].initButton(KB_X + 52,  y3, 36, KEY_H, ".");
  numKeys[idx++].initButton(KB_X + 90,  y3, 36, KEY_H, ",");
  numKeys[idx++].initButton(KB_X + 128, y3, 50, KEY_H, "?");
  numKeys[idx++].initButton(KB_X + 180, y3, 50, KEY_H, "!");
  numKeys[idx++].initButton(KB_X + 232, y3, 36, KEY_H, "'");
  numKeys[idx++].initButton(KB_X + 270, y3, 44, KEY_H, "BK");

  // Row 4: ABC SPACE SEND
  int y4 = KB_Y + 3 * (KEY_H + KEY_GAP);
  numKeys[idx++].initButton(KB_X + 0,   y4, MODE_W,  KEY_H, "ABC");
  numKeys[idx++].initButton(KB_X + 72,  y4, SPACE_W, KEY_H, "SPACE");
  numKeys[idx++].initButton(KB_X + 230, y4, SEND_W,  KEY_H, "SEND");

  kbDrawn = true;
}

// ----------------- Alpha keyboard -----------------

static void drawKeyboard() {
  tft.setRotation(1);
  ts.setRotation(1);

  drawHeader();

  int idx = 0;

  const char* r1 = capsLock ? "QWERTYUIOP" : "qwertyuiop";
  const char* r2 = capsLock ? "ASDFGHJKL"  : "asdfghjkl";
  const char* r3 = capsLock ? "ZXCVBNM"    : "zxcvbnm";

  // Row 1
  for (int i = 0; i < 10; i++) {
    int x = KB_X + i * (KEY_W + KEY_GAP);
    int y = KB_Y;
    char label[2] = { r1[i], '\0' };
    kbKeys[idx++].initButton(x, y, KEY_W, KEY_H, label);
  }

  // Row 2
  for (int i = 0; i < 9; i++) {
    int x = KB_X + 16 + i * (KEY_W + KEY_GAP);
    int y = KB_Y + (KEY_H + KEY_GAP);
    char label[2] = { r2[i], '\0' };
    kbKeys[idx++].initButton(x, y, KEY_W, KEY_H, label);
  }

  // Row 3
  int y3 = KB_Y + 2 * (KEY_H + KEY_GAP);

  kbKeys[idx++].initButton(KB_X + 0, y3, SHIFT_W, KEY_H, capsLock ? "CAP" : "^");

  for (int i = 0; i < 7; i++) {
    int x = KB_X + SHIFT_W + KEY_GAP + i * (KEY_W + KEY_GAP);
    char label[2] = { r3[i], '\0' };
    kbKeys[idx++].initButton(x, y3, KEY_W, KEY_H, label);
  }

  kbKeys[idx++].initButton(278, y3, BKSP_W, KEY_H, "BK");

  // Row 4
  int y4 = KB_Y + 3 * (KEY_H + KEY_GAP);
  kbKeys[idx++].initButton(KB_X + 0,   y4, MODE_W,  KEY_H, "123");
  kbKeys[idx++].initButton(KB_X + 72,  y4, SPACE_W, KEY_H, "SPACE");
  kbKeys[idx++].initButton(KB_X + 230, y4, SEND_W,  KEY_H, "SEND");

  kbDrawn = true;
}

// ----------------- Button helpers -----------------

bool keyboardBackPressed(const ScreenPoint& sp) {
  return kbBackBtn.isClicked(sp);
}

bool msgBtnPressed(const ScreenPoint& sp) {
  return msgBtn.isClicked(sp);
}

bool toBtnPressed(const ScreenPoint& sp) {
  return toBtn.isClicked(sp);
}

// ----------------- Public tick -----------------

bool keyboardTick(const ScreenPoint& sp, bool justTouched) {
  if (!kbDrawn) {
    if (alphaMode) drawKeyboard();
    else           drawNumberPad();
  }

  if (!justTouched) return false;

  Serial.print("x = ");
  Serial.print(sp.x);
  Serial.print(", y = ");
  Serial.println(sp.y);

  // Top-right toggle button
  if (switchkeyboardButton.isClicked(sp)) {
    alphaMode = !alphaMode;
    kbDrawn = false;
    if (alphaMode) drawKeyboard();
    else           drawNumberPad();
    return false;
  }

  // -------- NUMBER / SYMBOL MODE --------
  if (!alphaMode) {
    for (int i = 0; i < 30; i++) {
      if (!numKeys[i].isClicked(sp)) continue;

      // Row 1: digits 0..9
      if (i >= 0 && i <= 9) {
        appendChar("1234567890"[i]);
        return false;
      }

      // Row 2: - / : ; ( ) $ & @ "
      if (i >= 10 && i <= 19) {
        switch (i - 10) {
          case 0: appendChar('-'); break;
          case 1: appendChar('/'); break;
          case 2: appendChar(':'); break;
          case 3: appendChar(';'); break;
          case 4: appendChar('('); break;
          case 5: appendChar(')'); break;
          case 6: appendChar('$'); break;
          case 7: appendChar('&'); break;
          case 8: appendChar('@'); break;
          case 9: appendChar('"'); break;
        }
        return false;
      }

      // Row 3
      if (i == 20) { return false; }             // #+= placeholder
      if (i == 21) { appendChar('.'); return false; }
      if (i == 22) { appendChar(','); return false; }
      if (i == 23) { appendChar('?'); return false; }
      if (i == 24) { appendChar('!'); return false; }
      if (i == 25) { appendChar('\''); return false; }
      if (i == 26) { backspaceChar(); return false; }

      // Row 4
      if (i == 27) {   // ABC
        alphaMode = true;
        kbDrawn = false;
        drawKeyboard();
        return false;
      }

      if (i == 28) { appendChar(' '); return false; }

      if (i == 29) {   // SEND
        if (!msgOrNumber) {
          Serial.print("PHONE: ");
          Serial.println(typed);
          return true;
        } else {
          Serial.print("MSG: ");
          Serial.println(typed);
          return true;
        }
      }
    }
    return false;
  }

    // -------- ALPHA MODE --------
  for (int i = 0; i < 31; i++) {
    if (!kbKeys[i].isClicked(sp)) continue;

    const char* r1 = capsLock ? "QWERTYUIOP" : "qwertyuiop";
    const char* r2 = capsLock ? "ASDFGHJKL"  : "asdfghjkl";
    const char* r3 = capsLock ? "ZXCVBNM"    : "zxcvbnm";

    // Row 1
    if (i >= 0 && i <= 9) {
      appendChar(r1[i]);
      return false;
    }

    // Row 2
    if (i >= 10 && i <= 18) {
      appendChar(r2[i - 10]);
      return false;
    }

    // Row 3
    if (i == 19) {
      capsLock = !capsLock;
      kbDrawn = false;
      drawKeyboard();
      return false;
    }

    if (i >= 20 && i <= 26) {
      appendChar(r3[i - 20]);
      return false;
    }

    if (i == 27) {
      backspaceChar();
      return false;
    }

    // Row 4
    if (i == 28) {
      alphaMode = false;
      kbDrawn = false;
      drawNumberPad();
      return false;
    }

    if (i == 29) {
      appendChar(' ');
      return false;
    }

    if (i == 30) {
      if (msgOrNumber) {
        Serial.print("MSG: ");
        Serial.println(typed);
        return true;
      } else {
        Serial.print("PHONE: ");
        Serial.println(typed);
        return true;
      }
    }
  }
  return false;
}

// ----------------- Getters / reset -----------------

const char* keyboardGetText(void) {
  return typed;
}

void keyboardReset(void) {
  kbDrawn = false;
  typedLen = 0;
  typed[0] = '\0';

  msgOrNumber = false;   // start in phone number mode
  alphaMode = false;     // start in number/symbol mode
  capsLock = false; 
}