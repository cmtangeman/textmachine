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

static MessageThread threads[MAX_CONVERSATIONS];  // array of struct message thread which will contain each conversation
static int threadTop = -1;          // Most recent thread index. -1 means no threads.
static int convoSelection = 0;

static Button msgBackBtn;
static Button convoBackBtn;
static Button threadBtns[MAX_CONVERSATIONS];  // Initialize as much buttons as we might need 

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
      int msgOrder = threadTop - i;   // 0..N-1
      int y = listStartY + msgOrder * rowH;

      char label[24];
      int cidx = findContactName(threads[i].phoneNumber);



      if (cidx != -1) {
        copyBounded(label, contactList[cidx].name, sizeof(label));  // safely copy the phone number onto the label. 
      } else {
        copyBounded(label, threads[i].phoneNumber, sizeof(label));
      }

      // Print out convo option i 
      threadBtns[msgOrder].initButton(x, y, w, rowH, label);
      tft.setCursor(0, y + 8);
      tft.print(msgOrder + 1);
      tft.print(" ");

      // timestamp on the right side, smaller text
      tft.setTextSize(1);
      tft.setCursor(x + 130, y + 12);
      if (threads[i].lastMessageIndex >= 0) {
          tft.print(threads[i].messages[threads[i].lastMessageIndex].timestamp);
      }
      tft.setTextSize(2);  // restore

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

    MessageThread& t = threads[idx];  // MessageThread& , an adress?

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
  return -1; // No convo thread available, find a new one! 
}

static void moveThreadToTop(int idx) {
  if (idx == threadTop) return;  // already most recent

  MessageThread temp = threads[idx]; // temporarily sttore thread we want to push up

  for (int i = idx; i < threadTop; i++) {
    threads[i] = threads[i + 1];  // Push everything back
  }

  threads[threadTop] = temp;  // 
}

// -------------------------------------------------------------------------------------------------
// Public storage API
// -------------------------------------------------------------------------------------------------

void pushMessage(const char* phone, const char* text, MsgDir dir, const char* time) {
  int idx = findThreadByPhone(phone);

  // If the conversation DNE: Create new thread:
  if (idx == -1) {  
    if (threadTop + 1 >= MAX_CONVERSATIONS) {
      Serial.println("Conversations full, message not saved!");
      return;
    }

    threadTop++;
    idx = threadTop;

    copyBounded(threads[idx].phoneNumber, phone, MAX_PHONE_LEN);  // Assign the phone number to that thread
    threads[idx].lastMessageIndex = -1; // Now thread exists but no messages stored yet
    // threads[idx].lastMsgTimestamp = time;  
     
  }

  MessageThread& t = threads[idx];  // reference t for shortcut

  // For new / old thread/ If the conversation is not full, append new message
  if (t.lastMessageIndex + 1 < MAX_MESSAGES_PER_CONVO) {
    t.lastMessageIndex++; 

    // copy the individual message into the log of messages for this thread including its body, timestamp, and direction
    copyBounded(t.messages[t.lastMessageIndex].body, text, MAX_BODY_LEN);
    copyBounded(threads[idx].messages[t.lastMessageIndex].timestamp, time, MAX_TIMESTAMP_LEN);
    t.messages[t.lastMessageIndex].dir = dir;

  } else {
    // TODO
  }

  // Most recently active thread moves to top
  moveThreadToTop(idx);
  


}