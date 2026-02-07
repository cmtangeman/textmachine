#include "Messages.h"
#include <string.h>
#include <Arduino.h>
#include <stdlib.h>
#include <Adafruit_ILI9341.h>
#include "contacts.h"


// ---------------- STORAGE ----------------
// Uses the sizes from Messages.h (MAX_CONVERSATIONS, etc.)
static MessageThread threads[MAX_CONVERSATIONS];
static int threadTop = -1;   // Index of MOST RECENT thread (top of recents list). -1 means none.

// From main sketch (defined in your .ino)
extern int readSerial(char result[]);
extern void text(void);
extern void receive(void);
extern Adafruit_ILI9341 tft;

// Contacts list (defined elsewhere)
extern Contact contactList[MAX_CONTACTS];


// ---------------- HELPERS ----------------

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

// Safe bounded copy that GUARANTEES null-termination.
static void copyBounded(char *dst, const char *src, size_t dstSize) {
  if (dstSize == 0) return;
  strncpy(dst, src, dstSize - 1);
  dst[dstSize - 1] = '\0';
}


// ---------------- PUBLIC API ----------------

// Adds a message to the thread for `phone`.
// If the thread doesn't exist, it creates it (if capacity allows).
// Then it appends the message (if the thread message buffer has room).
// Finally, it promotes the thread to the top (most recent).
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


// Prints the "recents" list (threads) with 1-based numbering.
void viewRecents() {
  Serial.println("Messages:");

  for (int i = threadTop; i >= 0; i--) {
    Serial.print(threadTop - i + 1);
    Serial.print(". ");

    // Try to display a contact name instead of raw number
    int cidx = findContactName(threads[i].phoneNumber);

    if (cidx != -1) {
      Serial.println(contactList[cidx].name);
    } else {
      Serial.println(threads[i].phoneNumber);
    }
  }
}


// Opens a conversation by menu selection number (1 = most recent).
// This should NOT reorder anything (just viewing shouldn't change recents).
void openConversation(int selection) {
  // Convert menu selection (1..N) to thread array index (threadTop..0)
  if (selection <= 0) return;

  int idx = threadTop - (selection - 1);
  if (idx < 0 || idx > threadTop) return;

  MessageThread &t = threads[idx];

  Serial.println("\nConversation:");
  for (int i = t.lastMessageIndex; i >= 0; i--) {
    Serial.print(t.messages[i].dir == IN ? "< " : "> ");
    Serial.println(t.messages[i].body);
  }
}


// Your main menu UI.
// NOTE: I kept your options and behavior, but:
// - "C: Contacts" currently calls text() in your original (probably placeholder)
// - selection input uses atoi(), so non-numeric becomes 0 and is ignored
void messagesMenu() {
  Serial.println("N: New Message");
  Serial.println("C: Contacts");
  Serial.println("R: Refresh ");
  Serial.println("");
  tft.println("N: New Message");
  tft.println("C: Contacts");
  tft.println("R: Refresh ");
  tft.println("");
  viewRecents();

  char choice[3];
  readSerial(choice);

  switch (choice[0]) {
    case 'N':
      text();
      return;

    case 'C':
      // You probably want to show contacts menu here, but you had text() before.
      // Leaving as-is to preserve behavior.
      contactsMenu();
      return;

    case 'R':
      receive();
      return;
  }

  // If user typed a number, open that conversation
  int sel = atoi(choice);   // ascii to integer; non-numeric -> 0
  openConversation(sel);
}


// You declared convo() in the new .h, but it's unclear what it should do now.
// Keeping it as a stub so you don't get a linker error if something calls it.
void convo() {
  // TODO: Define what convo() should mean, or delete it from Messages.h
  // If you meant "print a conversation", you now have openConversation(selection).
}
