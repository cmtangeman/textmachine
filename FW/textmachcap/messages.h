#ifndef MESSAGES_H
#define MESSAGES_H

#include <Arduino.h>
#include <Adafruit_ILI9341.h>
#include "types.h"

// -------------------------------------------------------------------------------------------------
// Constants
// -------------------------------------------------------------------------------------------------

#define MAX_CONVERSATIONS       10
#define MAX_MESSAGES_PER_CONVO  15
#define MAX_PHONE_LEN           20
#define MAX_BODY_LEN            60
#define MAX_TIMESTAMP_LEN 20
#define MAX_MSG_RENDER_LINES    6   // cap on wrapped lines a single msgButton bubble will print

// -------------------------------------------------------------------------------------------------
// External display
// -------------------------------------------------------------------------------------------------

extern Adafruit_ILI9341 tft;

// -------------------------------------------------------------------------------------------------
// Types
// -------------------------------------------------------------------------------------------------

enum MsgDir {
  IN,
  OUT
};

struct Msg {
  char body[MAX_BODY_LEN];
  char timestamp[MAX_TIMESTAMP_LEN];
  MsgDir dir;
  bool saved;  // already written to SD — distinct from `kept` below
  bool kept;   // user tapped this message to keep it (Snapchat-style keep indicator)
  bool read; 
};

struct MessageThread {
  char phoneNumber[MAX_PHONE_LEN];
  Msg messages[MAX_MESSAGES_PER_CONVO]; // Instantiation of Msg/
  int lastMessageIndex;
  bool readThread;
};


// Helper
void normalizePhoneNumber(const char* input, char* output, int outLen);

// Word-wraps body against maxWidth (pixels, using the TFT's current font/size). If
// outLines is non-null, copies up to maxOutLines wrapped line strings into it (each
// bounded to MAX_BODY_LEN chars) — this is what msgButton::render() uses to actually
// print a bubble's text. Always returns the total line count, even if it exceeds
// maxOutLines.
int wrapMessageText(const char* body, int maxWidth, char outLines[][MAX_BODY_LEN], int maxOutLines);

// Returns the number of word-wrapped lines `body` takes up when rendered at maxWidth
// pixels (using the TFT's current font/size) — used to size a msgButton's height.
int measureMessageLines(const char* body, int maxWidth);

// notifications
// FIX: was declared `static` here, which gives every translation unit that
// includes this header its own private copy — textmachcap.ino calling it
// would fail to link against messages.cpp's definition. Needs external
// linkage since it's meant to be called from the menu screen.
bool unreadMessage();


// Storage
void pushMessage(const char* phone, const char* text, MsgDir dir, const char* time);

void saveMessageToSD(const char* phone, Msg& msg);

void unsaveMessageFromSD(const char* phone, int msgIdx);

void loadMessagesFromSD();

// Recent messages UI
int  recentMessagesScreen(const ScreenPoint& sp, bool justPressed);
bool msgBackBtnPressed(const ScreenPoint& sp);
void recentMessagesReset();

// Conversation UI
bool drawConversationToTFT(int selection,const ScreenPoint& sp, bool justPressed);
bool convoBackBtnPressed(const ScreenPoint& sp);
bool convoReplyBtnPressed(const ScreenPoint& sp);
const char* getConversationPhone(int selection);
void conversationReset();

#endif