#include "messages.h"
#include "types.h"
#include "buttons.h"
#include "contacts.h"

#include <string.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ILI9341.h>

// -------------------------------------------------------------------------------------------------
// External dependencies
// -------------------------------------------------------------------------------------------------

extern Adafruit_ILI9341 tft;
extern Contact contactList[MAX_CONTACTS];

// -------------------------------------------------------------------------------------------------
// Module state
// -------------------------------------------------------------------------------------------------

static MessageThread threads[MAX_CONVERSATIONS];
static int threadTop = -1;          // Most recent thread index. -1 means no threads.
static int convoSelection = 0;

static Button msgBackBtn;
static Button convoBackBtn;
static Button threadBtns[MAX_CONVERSATIONS];

static bool recentMessagesDrawn = false;
static bool conversationDrawn   = false;

// -------------------------------------------------------------------------------------------------
// Private helpers
// -------------------------------------------------------------------------------------------------

static int  findThreadByPhone(const char* phone);
static void moveThreadToTop(int idx);
static void copyBounded(char* dst, const char* src, size_t dstSize);

// -------------------------------------------------------------------------------------------------
// UI helpers
// -------------------------------------------------------------------------------------------------

bool msgBackBtnPressed(const ScreenPoint& sp) {
  return msgBackBtn.isClicked(sp);
}

bool convoBackBtnPressed(const ScreenPoint& sp) {
  return convoBackBtn.isClicked(sp);
}

void recentMessagesReset() {
  recentMessagesDrawn = false;
}

void conversationReset() {
  conversationDrawn = false;
}

// -------------------------------------------------------------------------------------------------
// Recent messages screen
// -------------------------------------------------------------------------------------------------

int recentMessagesScreen(const ScreenPoint& sp, bool justPressed) {
  const int listStartY = 50;
  const int rowH = 34;
  const int x = 10;
  const int w = 220;

  if (!recentMessagesDrawn) {
    tft.fillScreen(ILI9341_BLACK);
    tft.setTextColor(ILI9341_WHITE);
    tft.setTextSize(2);

    msgBackBtn.initButton(0, 0, 30, 30, "<");

    tft.setCursor(50, 10);
    tft.print("Recents");

    for (int i = threadTop; i >= 0; i--) {
      int selection = threadTop - i;   // 0..N-1
      int y = listStartY + selection * rowH;

      char label[24];
      int cidx = findContactName(threads[i].phoneNumber);

      if (cidx != -1) {
        copyBounded(label, contactList[cidx].name, sizeof(label));
      } else {
        copyBounded(label, threads[i].phoneNumber, sizeof(label));
      }

      threadBtns[selection].initButton(x, y, w, rowH, label);

      tft.setCursor(0, y + 8);
      tft.print(selection + 1);
      tft.print(" ");
    }

    recentMessagesDrawn = true;
  }

  if (!justPressed) return -1;

  for (int rowIndex = 0; rowIndex <= threadTop; rowIndex++) {
    if (threadBtns[rowIndex].isClicked(sp)) {
      convoSelection = rowIndex;
      return convoSelection;
    }
  }

  return -1;
}

// -------------------------------------------------------------------------------------------------
// Conversation screen
// -------------------------------------------------------------------------------------------------

bool drawConversationToTFT(int selection) {
  if (!conversationDrawn) {
    int idx = threadTop - selection;
    if (idx < 0 || idx > threadTop) return false;

    MessageThread& t = threads[idx];

    tft.fillScreen(ILI9341_BLACK);
    tft.setTextColor(ILI9341_WHITE);
    tft.setTextSize(2);

    convoBackBtn.initButton(0, 0, 30, 30, "<");

    tft.setCursor(0, 50);

    int cidx = findContactName(t.phoneNumber);
    if (cidx != -1) {
      tft.println(contactList[cidx].name);
    } else {
      tft.println(t.phoneNumber);
    }

    tft.println("----------------");

    const int maxShow = 6;
    int shown = 0;

    for (int i = t.lastMessageIndex; i >= 0 && shown < maxShow; i--, shown++) {
      tft.print(t.messages[i].dir == IN ? "< " : "> ");
      tft.println(t.messages[i].body);
    }

    conversationDrawn = true;
  }

  return true;
}

// -------------------------------------------------------------------------------------------------
// Storage helpers
// -------------------------------------------------------------------------------------------------

static void copyBounded(char* dst, const char* src, size_t dstSize) {
  if (dstSize == 0) return;

  strncpy(dst, src, dstSize - 1);
  dst[dstSize - 1] = '\0';
}

static int findThreadByPhone(const char* phone) {
  for (int i = 0; i <= threadTop; i++) {
    if (strcmp(threads[i].phoneNumber, phone) == 0) {
      return i;
    }
  }
  return -1;
}

static void moveThreadToTop(int idx) {
  if (idx == threadTop) return;  // already most recent

  MessageThread temp = threads[idx];

  for (int i = idx; i < threadTop; i++) {
    threads[i] = threads[i + 1];
  }

  threads[threadTop] = temp;
}

// -------------------------------------------------------------------------------------------------
// Public storage API
// -------------------------------------------------------------------------------------------------

void pushMessage(const char* phone, const char* text, MsgDir dir) {
  int idx = findThreadByPhone(phone);

  // Create new thread if needed
  if (idx == -1) {
    if (threadTop + 1 >= MAX_CONVERSATIONS) {
      Serial.println("Conversations full, message not saved!");
      return;
    }

    threadTop++;
    idx = threadTop;

    copyBounded(threads[idx].phoneNumber, phone, MAX_PHONE_LEN);
    threads[idx].lastMessageIndex = -1;
  }

  MessageThread& t = threads[idx];

  // Append message if space remains
  if (t.lastMessageIndex + 1 < MAX_MESSAGES_PER_CONVO) {
    t.lastMessageIndex++;

    copyBounded(t.messages[t.lastMessageIndex].body, text, MAX_BODY_LEN);
    t.messages[t.lastMessageIndex].dir = dir;
  } else {
    // Optional future improvement:
    // drop oldest message and keep newest instead of doing nothing
  }

  // Most recently active thread moves to top
  moveThreadToTop(idx);
}