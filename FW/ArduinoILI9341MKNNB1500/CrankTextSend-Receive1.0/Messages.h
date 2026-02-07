#ifndef MESSAGES_H
#define MESSAGES_H

#include <Arduino.h>

#define MAX_CONVERSATIONS 10
#define MAX_MESSAGES_PER_CONVO 20
#define MAX_PHONE_LEN 20
#define MAX_BODY_LEN 60

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
void pushMessage(const char *phone, const char *text, MsgDir dir);
void viewRecents();
void openConversation(int selection);
void messagesMenu();
void convo();


#endif
