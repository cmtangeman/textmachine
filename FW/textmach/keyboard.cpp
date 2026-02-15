#include "keyboard.h"
#include "buttons.h"
#include "types.h"
#include <Adafruit_ILI9341.h>


extern Adafruit_ILI9341 tft;

// ---------- Layout for ROTATION = 1 (landscape 320x240) ----------
static const int HEADER_H = 70; // Where the text is going to print

static const int KB_X = 6;
static const int KB_Y = HEADER_H + 10;   // keyboard starts below header

static const int KEY_W  = 28;            // wider keys for 320px width
static const int KEY_H  = 26;
static const int KEY_GAP = 2;

static const int MAX_TEXT = 160;



// Buttons: 10 + 9 + 7 + 3 = 29 
// static Button kbKeys[31];
static Button kbKeys[41];

static Button kbBackBtn;
static Button toButton;
static Button msgButton;
static Button switchkeyboardButton;


static bool kbDrawn = false;

static bool msgOrNumber = false;


// text buffer **?
static char typed[MAX_TEXT]; // Allocate 160 charaters for a message
static int typedLen = 0;

// Touch edge detect so holding stylus doesn't repeat
static bool kbTapEdge(bool touched) {
  static bool wasTouched = false;
  if (!touched) { wasTouched = false; return false; }
  if (wasTouched) return false;
  wasTouched = true;
  return true;
}

// Typing actual message content 
static void drawTypedLine() {
  // Clear typed line area ONLY (don’t wipe the header)
  // Typed line sits at y=44..68
  tft.fillRect(76, 0, tft.width() - 56, 34, ILI9341_WHITE);
  tft.fillRect(0, 44, tft.width(), 34, ILI9341_MAGENTA); // Active bar
  tft.setCursor(0, 46);
  tft.setTextSize(2);
  tft.setTextColor(ILI9341_BLACK);
  tft.print(typed);
}

// Typing phone numbers 
static void drawTypedLineNum() {
  // Clear typed line area ONLY (don’t wipe the header)
  // Typed line sits at y=44..68
  tft.fillRect(76, 0, tft.width() - 56, 34, ILI9341_MAGENTA); // Active Bar
  tft.fillRect(0, 44, tft.width(), 34, ILI9341_WHITE); 
  tft.setCursor(80, 0); // Active text
  tft.setTextSize(2);
  tft.setTextColor(ILI9341_BLACK);
  tft.print(typed);
}

static void appendChar(char c) {
  if (typedLen >= MAX_TEXT - 1) return;
  typed[typedLen++] = c;
  typed[typedLen] = '\0';

  Serial.print("KEY: ");
  Serial.println(c);
  if(msgOrNumber){
  drawTypedLine();
  }else{
  drawTypedLineNum();   // <- First we start off with the phone #
  }
}

static void backspaceChar() {
  if (typedLen <= 0) return;
  typedLen--;
  typed[typedLen] = '\0';
  Serial.println("KEY: <BKSP>");
  if (msgOrNumber) drawTypedLine();
  else             drawTypedLineNum();

}

void keyboardClearText(void) {
  typedLen = 0;
  typed[0] = '\0';
  msgOrNumber = true;
}



// Create and draw keyboard once
static void drawKeyboard() {
  // IMPORTANT: only do this if your whole app is using rotation 1.
  // If your touch mapping was calibrated for a different rotation, clicks will be wrong.
  tft.setRotation(1);

  tft.fillScreen(ILI9341_BLACK);
  tft.setTextColor(ILI9341_WHITE);
  tft.setTextSize(2);

  // Bac,
  kbBackBtn.initButton(6, 6, 36, 36, "<");


  tft.setCursor(6, 46);
  tft.print("To:");
  // typed line prints after "To:"
  typedLen = 0;
  typed[0] = '\0';
  if (msgOrNumber) drawTypedLine();
  else             drawTypedLineNum();


  int idx = 0;

  // Row 1: QWERTYUIOP (10)
  const char* r1 = "QWERTYUIOP";
  for (int i = 0; i < 10; i++) {
    int x = KB_X + i * (KEY_W + KEY_GAP);
    int y = KB_Y;
    char label[2] = { r1[i], '\0' };
    kbKeys[idx++].initButton(x, y, KEY_W, KEY_H, label);
  }

  // Row 2: ASDFGHJKL (9) slight indent
  const char* r2 = "ASDFGHJKL";
  for (int i = 0; i < 9; i++) {
    int x = KB_X + (KEY_W / 2) + i * (KEY_W + KEY_GAP);
    int y = KB_Y + (KEY_H + KEY_GAP);
    char label[2] = { r2[i], '\0' };
    kbKeys[idx++].initButton(x, y, KEY_W, KEY_H, label);
  }

  // Row 3: ZXCVBNM (7) more indent
  const char* r3 = "ZXCVBNM+-";
  for (int i = 0; i < 9; i++) {
    int x = KB_X + (KEY_W) + i * (KEY_W + KEY_GAP);
    int y = KB_Y + 2 * (KEY_H + KEY_GAP);
    char label[2] = { r3[i], '\0' };
    kbKeys[idx++].initButton(x, y, KEY_W, KEY_H, label);
  }

  // Bottom row: SPACE, BKSP, SEND (3)
  int yb = KB_Y + 3 * (KEY_H + KEY_GAP);
  

  kbKeys[idx++].initButton(KB_X + 0,   yb, 200, KEY_H, "SPACE");
  kbKeys[idx++].initButton(KB_X + 202, yb, 56,  KEY_H, "BKSP");
  kbKeys[idx++].initButton(KB_X + 260, yb, 54,  KEY_H, "SEND");

  // NEW: Digits row (10) below bottom row
  int yd = KB_Y + 4 * (KEY_H + KEY_GAP);
  const char* d = "1234567890";
  for (int i = 0; i < 10; i++) {
    int x = KB_X + i * (KEY_W + KEY_GAP);
    char label[2] = { d[i], '\0' };
    kbKeys[idx++].initButton(x, yd, KEY_W, KEY_H, label); // indices 31..40
  }

  kbDrawn = true;
}

// Optional helper so your UI can handle back without changing keyboardTick signature
bool keyboardBackPressed(const ScreenPoint& sp) {
  return kbBackBtn.isClicked(sp);
}

// Call this every loop when keyboard active.
// Returns true if "SEND" pressed.
bool keyboardTick(const ScreenPoint& sp, bool touched) {
  if (!kbDrawn) drawKeyboard();
  if (!kbTapEdge(touched)) return false;

  // Back button click -- handled in UI state instead)
  /*if (keyboardBackPressed(sp)) {
    Serial.println("KEY: <BACK>");
    return false;
  }*/ 

  // Check each key and check if inputted was one of the clicked keys
  for (int i = 0; i < 41; i++) {
    if (!kbKeys[i].isClicked(sp)) continue; // EX : key 3 is clicked so we 

if (i <= 27) {
  char c;
  if (i < 10)        c = "QWERTYUIOP"[i];
  else if (i < 19)   c = "ASDFGHJKL"[i - 10];
  else if (i <28)         c = "ZXCVBNM+-"[i - 19];
  appendChar(c);
  return false;
}

if (i == 28) { appendChar(' '); return false; }
if (i == 29) { backspaceChar(); return false; }
if (i == 30) { Serial.println(typed); return true; }

  if (i >= 31 && i <= 40) {
    char c = "1234567890"[i - 31];
    appendChar(c);
    return false;
  }
}
}

const char* keyboardGetText(void) {
  return typed;
}

void keyboardReset(void) {
  kbDrawn = false;
  typedLen = 0;
  typed[0] = '\0';
  msgOrNumber = false;
}
