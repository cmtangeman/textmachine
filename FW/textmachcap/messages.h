#ifndef MESSAGES_H
#define MESSAGES_H

#include <Arduino.h>
#include <Adafruit_ILI9341.h>
#include "types.h"

// -------------------------------------------------------------------------------------------------
// Constants
// -------------------------------------------------------------------------------------------------

#define MAX_CONVERSATIONS       10
#define MAX_MESSAGES_PER_CONVO  10
#define MAX_PHONE_LEN           20
#define MAX_BODY_LEN            60
#define MAX_TIMESTAMP_LEN 20

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
};

struct MessageThread {
  char phoneNumber[MAX_PHONE_LEN];
  Msg messages[MAX_MESSAGES_PER_CONVO]; // Instantiation of Msg/
  int lastMessageIndex;
};

// -------------------------------------------------------------------------------------------------
// Public API
// -------------------------------------------------------------------------------------------------

// Storage
void pushMessage(const char* phone, const char* text, MsgDir dir, const char* time);

// Recent messages UI
int  recentMessagesScreen(const ScreenPoint& sp, bool justPressed);
bool msgBackBtnPressed(const ScreenPoint& sp);
void recentMessagesReset();

// Conversation UI
bool drawConversationToTFT(int selection);
bool convoBackBtnPressed(const ScreenPoint& sp);
void conversationReset();

#endif