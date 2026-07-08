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
  bool saved;
};

struct MessageThread {
  char phoneNumber[MAX_PHONE_LEN];
  Msg messages[MAX_MESSAGES_PER_CONVO]; // Instantiation of Msg/
  int lastMessageIndex;
};


// Helper
void normalizePhoneNumber(const char* input, char* output, int outLen);

// Returns the number of word-wrapped lines `body` takes up when rendered at maxWidth
// pixels (using the TFT's current font/size) — used to size a msgButton's height.
int measureMessageLines(const char* body, int maxWidth);


// Storage
void pushMessage(const char* phone, const char* text, MsgDir dir, const char* time);

void saveMessageToSD(const char* phone, Msg& msg);

void loadMessagesFromSD();

// Recent messages UI
int  recentMessagesScreen(const ScreenPoint& sp, bool justPressed);
bool msgBackBtnPressed(const ScreenPoint& sp);
void recentMessagesReset();

// Conversation UI
bool drawConversationToTFT(int selection,const ScreenPoint& sp, bool justPressed);
bool convoBackBtnPressed(const ScreenPoint& sp);
void conversationReset();

#endif