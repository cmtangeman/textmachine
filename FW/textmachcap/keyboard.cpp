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

static int operatingMode;

// ── iOS-style color palette ───────────────────────────────────────
// These are initialized in drawKeyboard/drawNumberPad after tft is ready
static uint16_t COL_KEY;       // regular key: dark gray
static uint16_t COL_SPECIAL;   // shift, BK, 123, ABC: slightly lighter gray
static uint16_t COL_SEND;      // iOS blue
static uint16_t COL_KB_BG;     // keyboard background: near black
static uint16_t COL_FIELD;     // input field: white
static uint16_t COL_HEADER;    // header bar background

static bool colorsInit = false;

static bool symMode = false;
static const int SYM_W = 46;
static Button symKeys[20];

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
static Button numKeys[13];
static Button kbBackBtn;
static Button toBtn;
static Button msgBtn;
static Button switchkeyboardButton;
static Button nameBtn;
static Button numberBtn;

static bool kbDrawn     = false;
static bool msgOrNumber = false;
static bool alphaMode   = false;
static bool capsLock    = false;

static char frozenToField[MAX_PHONE_LEN] = "";
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
  switch (operatingMode) {

    case KB_COMPOSE:
      tft.fillRect(76, 0, tft.width() - 76, 34, COL_FIELD);
      if (frozenToField[0] != '\0') {
        tft.setCursor(80, 8);
        uiUseDefaultFont();
        tft.setTextColor(ILI9341_BLACK);
        tft.print(frozenToField);
      }
      tft.fillRect(0, 44, tft.width(), 34, COL_FIELD);
      tft.setCursor(4, 46);
      uiUseDefaultFont();
      tft.setTextColor(ILI9341_BLACK);
      tft.print(typed);
      drawCaret(4, 46);
      break;

    case KB_ADD_CONTACT:
      tft.fillRect(76, 2,  tft.width() - 78, 30, COL_FIELD);
      if (frozenToField[0] != '\0') {
        tft.setCursor(80, 8);
        uiUseDefaultFont();
        tft.setTextColor(ILI9341_BLACK);
        tft.print(frozenToField);
      }
      tft.fillRect(76, 38, tft.width() - 78, 30, COL_FIELD);
      tft.setCursor(80, 44);
      uiUseDefaultFont();
      tft.setTextColor(ILI9341_BLACK);
      tft.print(typed);
      drawCaret(80, 44);
      break;

    case KB_DEBUG:
      tft.fillRect(0, 72, tft.width(), 28, COL_FIELD);
      tft.setCursor(4, 76);
      uiUseDefaultFont();
      tft.setTextColor(ILI9341_BLACK);
      tft.print(typed);
      drawCaret(4, 76);
      break;

    default:
      break;
  }
}

static void drawTypedLineNum() {
  switch (operatingMode) {

    case KB_COMPOSE:
      tft.fillRect(76, 0, tft.width() - 76, 34, COL_FIELD);
      tft.fillRect(0,  44, tft.width(),      34, COL_FIELD);
      tft.setCursor(80, 8);
      uiUseDefaultFont();
      tft.setTextColor(ILI9341_BLACK);
      tft.print(typed);
      drawCaret(80, 8);
      break;

    case KB_ADD_CONTACT:
      tft.fillRect(76, 2,  tft.width() - 78, 30, COL_FIELD);  // clear # field
      tft.fillRect(76, 38, tft.width() - 78, 30, COL_FIELD);  // clear Name field
      tft.setCursor(80, 8);
      uiUseDefaultFont();
      tft.setTextColor(ILI9341_BLACK);
      tft.print(typed);
      drawCaret(80, 8);
      break;

    case KB_DEBUG:
      tft.fillRect(0, 72, tft.width(), 28, COL_FIELD);
      tft.setCursor(4, 76);
      uiUseDefaultFont();
      tft.setTextColor(ILI9341_BLACK);
      tft.print(typed);
      drawCaret(4, 76);
      break;

    default:
      break;
  }
}

static void redrawTypedArea() {
  if (msgOrNumber) 
  drawTypedLine();
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

// FIX: `operatingMode` (used by drawKeyboard()/redrawTypedArea() to pick
// which header — To:/Msg: vs #:/Name: — to draw) was only ever updated by
// keyboardTick(), never by this function. So calling this right after a
// *different* mode's keyboard was last drawn (e.g. add-contact, then
// immediately texting the contact you just added) left the old header on
// screen: `kbDrawn` was already true, so drawKeyboard()'s `if (!kbDrawn)`
// header-redraw was skipped entirely, and redrawTypedArea() painted the
// frozen field into the *previous* mode's field rects. The underlying
// compose logic still worked correctly (hence "you can text from that
// screen") — only the header/fields stayed visually stuck on add-contact.
void keyboardSwitchToMessageField(const char* keepInToField, int mode) {
  operatingMode = mode;
  kbDrawn = false;
  alphaMode = true;
  clearTypedBuffer();
  msgOrNumber = true;
  copyBounded(frozenToField, keepInToField, MAX_PHONE_LEN);
  redrawTypedArea();  // paint frozenToField into header
  drawKeyboard();     // swap numpad → alpha keys
}

void keyboardClearText(void) {
  clearTypedBuffer();
  // means that we are now in the number field
  msgOrNumber = true;
  redrawTypedArea();
}

// ---------- Different header variations ----------

static void drawTextHeader() {
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
  if(operatingMode == KB_COMPOSE){
  tft.print("To:");
  }
  else{
    tft.print("Phone");
  }
  tft.setCursor(6, 42);
  tft.print("Msg:");

  // Buttons on header
  kbBackBtn.initButton(2, 2, 28, 28, "<", COL_SPECIAL);
  toBtn.initButton(76, 2, tft.width() - 78, 30, "", COL_FIELD);
  msgBtn.initButton(0, 38, tft.width(), 28, "", COL_FIELD);
  // switchkeyboardButton.initButton(tft.width() - 50, 38, 48, 24, alphaMode ? "123" : "ABC", COL_SPECIAL);

  redrawTypedArea();
}


static void drawContactHeader() {
  initColors();

  // Full screen background
  tft.fillScreen(COL_KB_BG);

  // Header bar
  tft.fillRect(0, 0, tft.width(), HEADER_H - 4, COL_HEADER);

  // Input fields
  tft.fillRect(76, 2,  tft.width() - 78, 30, COL_FIELD);
  tft.fillRect(76, 38, tft.width() - 78, 30, COL_FIELD);

  // Labels
  tft.setTextColor(ILI9341_WHITE);
  tft.setTextSize(2);
  tft.setCursor(34, 6);
  tft.print("#:");
  tft.setCursor(6, 42);
  tft.print("Name:");

  // Buttons
  kbBackBtn.initButton(2,  2,  28,              28, "<", COL_SPECIAL);
  numberBtn.initButton(76, 2,  tft.width() - 78, 30, "", COL_FIELD);
  nameBtn.initButton(  76, 38, tft.width() - 78, 30, "", COL_FIELD);

  redrawTypedArea();
}

static void drawDebugHeader() {
  initColors();

  // Full screen background
  tft.fillScreen(COL_KB_BG);

  
  // Input field
  tft.fillRect(76, 2,  tft.width() - 78, 30, COL_FIELD);

  // Output field
  tft.fillRect(76, 38, tft.width() - 78, 30, COL_FIELD);

  // Labels
  tft.setTextColor(ILI9341_WHITE);
  tft.setTextSize(2);
  tft.setCursor(34, 6);
  tft.print("#:");
  tft.setCursor(6, 42);
  tft.print("Name:");

  // Buttons
  kbBackBtn.initButton(2,  2,  28,              28, "<", COL_SPECIAL);


  redrawTypedArea();
}
// ---------- 3x4 Numpad ----------

static const int NUM_HALF_W = 37;

static void drawNumberPad() {
  if (!kbDrawn) {
    if (operatingMode == KB_ADD_CONTACT) drawContactHeader();
    else if (operatingMode == KB_COMPOSE) drawTextHeader();
    else                                  drawDebugHeader();
  }
  else tft.fillRect(0, KB_Y, tft.width(), tft.height() - KB_Y, COL_KB_BG);
  initColors();

  const char* labels[9] = {
    "1","2","3",
    "4","5","6",
    "7","8","9"
  };

  // Rows 0-2: normal 3x3 grid
  for (int row = 0; row < 3; row++) {
    for (int col = 0; col < 3; col++) {
      int i = row * 3 + col;
      int x = KB_X + col * (NUM_W + NUM_GAP);
      int y = KB_Y + row * (NUM_H + NUM_GAP);
      numKeys[i].initButton(x, y, NUM_W, NUM_H, labels[i], COL_KEY);
    }
  }

  // Row 3: ABC (half) | !@# (half) | 0 | BK
  int y4 = KB_Y + 3 * (NUM_H + NUM_GAP);
  numKeys[9].initButton( KB_X,                                    y4, NUM_HALF_W,          NUM_H, "ABC", COL_SPECIAL);
  numKeys[10].initButton(KB_X + NUM_HALF_W + NUM_GAP,             y4, NUM_HALF_W,          NUM_H, "!@#", COL_SPECIAL);
  numKeys[11].initButton(KB_X + (NUM_W + NUM_GAP),                y4, NUM_W,               NUM_H, "0",   COL_KEY);
  numKeys[12].initButton(KB_X + 2 * (NUM_W + NUM_GAP),            y4, NUM_W,               NUM_H, "BK",  COL_SPECIAL);

  kbDrawn = true;
}

// ---------- Alpha keyboard ----------

static void drawKeyboard() {

  if (!kbDrawn) {
    if      (operatingMode == KB_ADD_CONTACT) drawContactHeader();
else if (operatingMode == KB_DEBUG)       drawDebugHeader();
else                                      drawTextHeader();
  }


  else tft.fillRect(0, KB_Y, tft.width(), tft.height() - KB_Y, COL_KB_BG);
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

// ---------- Symbol Mode----- 

static void drawSymbolPad() {
  if (!kbDrawn) {
    if      (operatingMode == KB_ADD_CONTACT) drawContactHeader();
else if (operatingMode == KB_DEBUG)       drawDebugHeader();
else                                      drawTextHeader();
  }
  else tft.fillRect(0, KB_Y, tft.width(), tft.height() - KB_Y, COL_KB_BG);
  initColors();

  const char* labels[20] = {
    "!","@","#","$","%",
    "+","-","=","_","/",
    "(",")",",",".","?",
    "ABC","\"","'","*","BK"
  };

  for (int row = 0; row < 4; row++) {
    for (int col = 0; col < 5; col++) {
      int i = row * 5 + col;
      int x = KB_X + col * (SYM_W + KEY_GAP);
      int y = KB_Y + row * (KEY_H + KEY_GAP);
      uint16_t c = (i == 15) ? COL_SPECIAL :   // ABC
                   (i == 19) ? COL_SPECIAL :   // BK
                   COL_KEY;
      symKeys[i].initButton(x, y, SYM_W, KEY_H, labels[i], c);
    }
  }

  kbDrawn = true;
}

// ---------- Button helpers ----------

bool keyboardBackPressed(const ScreenPoint& sp) { return kbBackBtn.isClicked(sp); }
bool msgBtnPressed(const ScreenPoint& sp)        { return msgBtn.isClicked(sp); }
bool toBtnPressed(const ScreenPoint& sp)         { return toBtn.isClicked(sp); }
bool nameBtnPressed(const ScreenPoint& sp)   { return nameBtn.isClicked(sp); }
bool numberBtnPressed(const ScreenPoint& sp) { return numberBtn.isClicked(sp); }

// ---------- Public tick ----------

bool keyboardTick(const ScreenPoint& sp, bool justTouched, int mode) {
  operatingMode = mode;
  if (!kbDrawn) {
    if (alphaMode) drawKeyboard();
    else           drawNumberPad();
  }

  if (!justTouched) return false;

  Serial.print("x="); Serial.print(sp.x);
  Serial.print(" y="); Serial.println(sp.y);

  if (switchkeyboardButton.isClicked(sp)) {
    alphaMode = !alphaMode;
    // kbDrawn   = false;
    if (alphaMode) drawKeyboard(); else drawNumberPad();
    return false;
  }

  // in keyboardTick, add after the alphaMode check:

// -------- SYMBOL MODE --------
if (symMode) {
  for (int i = 0; i < 20; i++) {
    if (!symKeys[i].isClicked(sp)) continue;

    const char* syms[] = {
      "!","@","#","$","%",
      "+","-","=","_","/",
      "(",")",",",".","?",
      "ABC","\"","'","*","BK"
    };

    if (i == 15) {  // ABC → back to alpha
      symMode   = false;
      alphaMode = true;
      drawKeyboard();
      return false;
    }
    if (i == 19) { backspaceChar(); return false; }  // BK

    // all other keys — append the symbol
    // single char symbols
    const char* s = syms[i];
    if (s[1] == '\0') {
      appendChar(s[0]);
    } else {
      // multi char like " — append first char
      appendChar(s[0]);
    }
    return false;
  }
  return false;
}

  // -------- NUMPAD MODE --------
if (!alphaMode && !symMode) {
  for (int i = 0; i < 13; i++) {
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
    if (i == 9)  { alphaMode = true;  drawKeyboard();   return false; }  // ABC
    if (i == 10) { symMode   = true;  drawSymbolPad();  return false; }  // !@#
    if (i == 11) { appendChar('0');   return false; }
    if (i == 12) { backspaceChar();   return false; }
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
    if (i == 19) { capsLock = !capsLock; /*kbDrawn = false; */drawKeyboard(); return false; } 
    if (i >= 20 && i <= 26) { appendChar(r3[i - 20]); return false; }
    if (i == 27) { backspaceChar(); return false; }
    if (i == 28) { alphaMode = false; /*kbDrawn = false; */drawNumberPad(); return false; }
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
  symMode = false;
  typed[0]    = '\0';
  frozenToField[0] = '\0';
  msgOrNumber = false;
  alphaMode   = false;
  capsLock    = false;

}

// Forces the next keyboardTick() to fully repaint (e.g. after GRAM was lost
// to a screen power cycle) without touching whatever the user has typed.
void keyboardForceRedraw(void) {
  kbDrawn = false;
}

void debugPrint(const char* cmd, const char* response) {
  tft.fillRect(0, 0, tft.width(), 36, COL_KB_BG);
  tft.setTextSize(1);
  tft.setTextColor(ILI9341_WHITE);

  // echo command
  tft.setCursor(0, 2);
  tft.print("> ");
  tft.print(cmd);

  // response below
  tft.setCursor(0, 14);
  tft.print(response);
}