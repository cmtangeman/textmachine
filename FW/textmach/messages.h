#ifndef MESSAGES_H
#define MESSAGES_H

#include <Arduino.h>
#include "types.h"

#define MAX_CONVERSATIONS 10
#define MAX_MESSAGES_PER_CONVO 20
#define MAX_PHONE_LEN 20
#define MAX_BODY_LEN 60

#include <Adafruit_ILI9341.h>
extern Adafruit_ILI9341 tft;


// Const
enum MsgDir {
  IN,
  OUT
};

struct Msg {
  char body[MAX_BODY_LEN];
  MsgDir dir;
};

struct MessageThread {
  char phoneNumber[MAX_PHONE_LEN];
  Msg messages[MAX_MESSAGES_PER_CONVO];
  int lastMessageIndex;
};


// API
int recentMessagesScreen(const ScreenPoint& sp, bool touched);
void pushMessage(const char *phone, const char *text, MsgDir dir);

bool msgBackBtnPressed(const ScreenPoint& sp);
bool drawConversationToTFT(const ScreenPoint& sp,bool touched, int selection, bool drawn); // open convo
void uiTick(const ScreenPoint& sp, bool touched);

void recentMessagesReset(void);


#endif