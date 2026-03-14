#include "messages.h"
#include "types.h"
#include "buttons.h"
#include "contacts.h"
#include <string.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ILI9341.h>

extern Adafruit_ILI9341 tft;
static MessageThread threads[MAX_CONVERSATIONS];
static int threadTop = -1;   // Index of MOST RECENT thread (top of recents list). -1 means none.
static int convoSelection = 0;


// ---------------- STORAGE ----------------
// Uses the sizes from Messages.h (MAX_CONVERSATIONS, etc.)


// From main sketch (defined in your .ino)
// extern int readSerial(char result[]);
// extern void text(char *remoteNum);
// extern void receive(void);
// Contacts list (defined elsewhere)
extern Contact contactList[MAX_CONTACTS];

extern Adafruit_ILI9341 tft;
static Button msgBackBtn;

static bool drawn = false;

// ----- Forward declarations (so recentMessagesScreen can call these) -----
void drawConversationToTFT(int selection);
static int  findThreadByPhone(const char* phone);
static void moveThreadToTop(int idx);
static void copyBounded(char* dst, const char* src, size_t dstSize);

bool msgBackBtnPressed(const ScreenPoint& sp) {
  return msgBackBtn.isClicked(sp);
}


int recentMessagesScreen(const ScreenPoint& sp, bool touched) {
  
  static Button threadBtns[MAX_CONVERSATIONS];
  const int listStartY = 50;
  const int rowH = 34;
  const int x = 10;
  const int w = 220;

  if (!drawn) {
    tft.fillScreen(ILI9341_BLACK);
    tft.setTextColor(ILI9341_WHITE);
    tft.setTextSize(2);
  
    msgBackBtn.initButton(6, 6, 36, 36, "<");

    tft.setCursor(50, 10);
    
    tft.print("Recents");

    // Build/draw buttons for each thread
    for (int i = threadTop; i >= 0; i--) {
      int selection = threadTop - i + 1;  // 1..N
      int rowIndex = selection - 1;
      int y = listStartY + rowIndex * rowH;

      char label[24];
      int cidx = findContactName(threads[i].phoneNumber);
      if (cidx != -1) {
        strncpy(label, contactList[cidx].name, sizeof(label) - 1);
      } else {
        strncpy(label, threads[i].phoneNumber, sizeof(label) - 1);
      }
      label[sizeof(label) - 1] = '\0';

      threadBtns[rowIndex].initButton(x, y, w, rowH, label);

      // optional numbering
      tft.setCursor(0, y + 8);
      tft.print(selection);
      tft.print(" ");
    }

    drawn = true;
  }

  // Check if button is clicked : 
  for (int rowIndex = 0; rowIndex <= threadTop; rowIndex++) {
    if (threadBtns[rowIndex].isClicked(sp)) {
      convoSelection = rowIndex + 1; // 1..N
      drawn = false;
      return convoSelection;
    }
  }
  return -1;

  
}

void recentMessagesReset() {
    static bool drawn = false; // can't access static from outside
}

// Once the selection is true, print out the conversation. 
// TODO: Add a button to text and pass the phone number to the text program so all
// that's left is to add the message

bool drawConversationToTFT(const ScreenPoint& sp, bool touched, int selection, bool drawn) {
  bool isDrawn = drawn;
  if (!isDrawn){
  int idx = threadTop - (selection - 1);
  if (idx < 0 || idx > threadTop) return true; // TODO: Error case / scroll wheel eventually

  MessageThread &t = threads[idx];
  // Display 
  tft.fillScreen(ILI9341_BLACK);
  msgBackBtn.initButton(0, 0, 36, 36, "<");
  tft.setCursor(0, 50);
  tft.setTextSize(2);
  tft.setTextColor(ILI9341_WHITE);
  
  // Header: name/number
  int cidx = findContactName(t.phoneNumber);
  if (cidx != -1) tft.println(contactList[cidx].name);
  else            tft.println(t.phoneNumber);

  tft.println("----------------");

  // Show last few messages (fit screen)
  int maxShow = 6;
  int shown = 0;
  for (int i = t.lastMessageIndex; i >= 0 && shown < maxShow; i--, shown++) {
    tft.print(t.messages[i].dir == IN ? "< " : "> ");
    tft.println(t.messages[i].body);
  }
  isDrawn = true;
  }

  if(msgBackBtn.isClicked(sp) && touched){
    return true; 
  }

  return false;
}



// Non display message storage logic 
//--------------------------------------------------------------------------------------------------------------------------------------------

// Safe bounded copy that GUARANTEES null-termination.
static void copyBounded(char *dst, const char *src, size_t dstSize) {
  if (dstSize == 0) return;
  strncpy(dst, src, dstSize - 1);
  dst[dstSize - 1] = '\0';
}

// Find an existing thread by phone number.
// Returns index [0..threadTop], or -1 if not found.
static int findThreadByPhone(const char *phone) {
  for (int i = 0; i <= threadTop; i++) {
    if (strcmp(threads[i].phoneNumber, phone) == 0) {
      return i;
    }
  }
  return -1;
}
// Moves the selected thread to the top of the recents list (index = threadTop).
// This is the iMessage behavior: ONLY call this after send/receive (new activity).
static void moveThreadToTop(int idx) {
  if (idx == threadTop) return;   // already most recent

  MessageThread temp = threads[idx];

  // Shift everything between idx..threadTop-1 down by one slot
  for (int i = idx; i < threadTop; i++) {
    threads[i] = threads[i + 1];
  }

  // Put the selected thread at the top (most recent)
  threads[threadTop] = temp;
}

void pushMessage(const char *phone, const char *text, MsgDir dir) {
  int idx = findThreadByPhone(phone);

  // If there is no existing thread with this phone number, create a new one.
  if (idx == -1) {
    if (threadTop + 1 >= MAX_CONVERSATIONS) {
      Serial.println("Conversations full, message not saved!");
      return;
    }

    threadTop++;
    idx = threadTop;  // New thread lives at the top by default

    copyBounded(threads[idx].phoneNumber, phone, MAX_PHONE_LEN);
    threads[idx].lastMessageIndex = -1; // Empty thread
  }

  MessageThread &t = threads[idx];  // Reference to the thread in storage (no copying)

  // Add message if there's room
  if (t.lastMessageIndex + 1 < MAX_MESSAGES_PER_CONVO) {
    t.lastMessageIndex++;

    copyBounded(t.messages[t.lastMessageIndex].body, text, MAX_BODY_LEN);
    t.messages[t.lastMessageIndex].dir = dir;
  } else {
    // If you want iMessage-like behavior, you probably want to drop OLDEST and keep newest,
    // but for now we just silently stop adding once full.
    // Serial.println("Thread message buffer full!");
  }

  // iMessage behavior: thread becomes most recent AFTER send/receive.
  moveThreadToTop(idx);
}

