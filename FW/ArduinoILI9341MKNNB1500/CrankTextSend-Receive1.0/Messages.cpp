#include "Messages.h"
#include <string.h>
#include <Arduino.h>
#include <stdlib.h>
#include "contacts.h"


// ---------------- CONFIG ----------------
#define MAX_CONVERSATIONS 2
#define MAX_MESSAGES_PER_CONVO 10
#define MAX_PHONE_LEN 20
#define MAX_BODY_LEN 30

// ---------------- TYPES ----------------



// ---------------- STORAGE ----------------
static Conversation conversations[MAX_CONVERSATIONS];
static int convoTop = -1;

// From main sketch
extern int readSerial(char result[]);
extern void text(void);
extern void receive(void);

extern Contact contactList[MAX_CONTACTS];

// ---------------- HELPERS ----------------
int findConversation(const char *phone) {
  for (int i = 0; i <= convoTop; i++) {
    if (strcmp(conversations[i].phone, phone) == 0)
      return i;
  }
  return -1;
}

void moveConversationToTop(int idx) {
  if (idx == convoTop) return;

  Conversation temp = conversations[idx];

  for (int i = idx; i < convoTop; i++) {
    conversations[i] = conversations[i + 1];
  }

  conversations[convoTop] = temp;
}

void pushMessage(const char *phone, const char *text, MsgDir dir) { // Use pointers for O(1) instead of O(n) time complexity
  int idx = findConversation(phone);

  if (idx == -1) {
    if (convoTop + 1 >= MAX_CONVERSATIONS){
      Serial.println("Conversations full, message not saved!");
      return; 
    }
  // Else if we still have room proceed with adding a new message. 
  
    convoTop++;
    idx = convoTop;

    strncpy(conversations[idx].phone, phone, MAX_PHONE_LEN);
    conversations[idx].top = -1;
  }

  Conversation &c = conversations[idx];

  if (c.top + 1 < MAX_MESSAGES_PER_CONVO) {
    c.top++;
    strncpy(c.msgs[c.top].body, text, MAX_BODY_LEN);
    c.msgs[c.top].dir = dir;
  }

  moveConversationToTop(idx);
}

// ---------------- PUBLIC API ----------------
void messagesInit() {
  convoTop = -1;
}

void pushIncomingMessage(const char *phone, const char *text) {
  pushMessage(phone, text, IN);
}

void pushOutgoingMessage(const char *phone, const char *text) {
  pushMessage(phone, text, OUT);
}

// ---------------- UI ----------------
void messagesMenu() {

  Serial.println("Type n for new message");
  Serial.println("Type c for contacts");
  Serial.println("Type r for refresh: ");
  Serial.println("/nSelect conversation number:");
 
  
  for (int i = convoTop; i >= 0; i--) {
    if(convoTop )
    Serial.print(convoTop - i + 1);
    Serial.print(". ");
    
  int idx = findContactName(conversations[i].phone);

  if (idx != -1) {
    Serial.println(contactList[idx].name);
  } else {
    Serial.println(conversations[i].phone);
  }
  }

  

  char choice[3];
  readSerial(choice);

  if(choice[0] == 'n'){
    text();
    return;
  }else if( choice[0] == 'c'){
    contactsMenu();
    return;
  }else if( choice[0] == 'r'){
    receive();
    return;
  }
  // If they are selecting one of the conversations, here is the logic that prints out that conversation:

  int sel = atoi(choice); // ascii to integer
  int idx = convoTop - (sel - 1);

  if (idx < 0 || idx > convoTop) return; // Invalid cases

  Conversation &c = conversations[idx];

  Serial.println("\nConversation:");
  for (int i = c.top; i >= 0; i--) {
    if (c.msgs[i].dir == IN) Serial.print("< ");
    else Serial.print("> ");
    Serial.println(c.msgs[i].body);
  }
}
