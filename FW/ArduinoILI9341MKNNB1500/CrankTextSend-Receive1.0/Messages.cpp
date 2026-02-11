#include "Messages.h"
#include <string.h>
#include <Arduino.h>
#include <stdlib.h>
#include <Adafruit_ILI9341.h>

#include <unistd.h>



#include "contacts.h"
#include "buttons.h"

// ---------------- STORAGE ----------------
// Uses the sizes from Messages.h (MAX_CONVERSATIONS, etc.)
static MessageThread threads[MAX_CONVERSATIONS];
static int threadTop = -1;   // Index of MOST RECENT thread (top of recents list). -1 means none.

// From main sketch (defined in your .ino)
extern int readSerial(char result[]);
extern void text(char *remoteNum);
extern void receive(void);
extern Adafruit_ILI9341 tft;

// Contacts list (defined elsewhere)
extern Contact contactList[MAX_CONTACTS];


// ---------------- HELPERS ----------------


enum UiScreen { UI_MENU, UI_RECENTS, UI_CONVO, UI_NEWTEXT };
static UiScreen uiScreen = UI_MENU;

static int convoSelection = 0;   // 1..N

static bool tapEdge(bool touched) {
  static bool wasTouched = false;
  if (!touched) { wasTouched = false; return false; }
  if (wasTouched) return false;
  wasTouched = true;
  return true;
}

static Button backBtn;

static void drawBackButton() {
  backBtn.initButton(6, 6, 36, 36, "<");
}

static bool backClicked(const ScreenPoint& sp) {
  return backBtn.isClicked(sp);
}

void uiTick(const ScreenPoint& sp, bool touched);

// Forward decls
static void menuScreen(const ScreenPoint& sp, bool touched);
static void recentsScreen(const ScreenPoint& sp, bool touched);
static void convoScreen(const ScreenPoint& sp, bool touched);
static void textScreen(const ScreenPoint& sp, bool touched);

void uiTick(const ScreenPoint& sp, bool touched) {
  switch (uiScreen) {
    case UI_MENU:    menuScreen(sp, touched);    break;
    case UI_RECENTS: recentsScreen(sp, touched); break;
    case UI_CONVO:   convoScreen(sp, touched);   break;
    case UI_NEWTEXT: textScreen(sp, touched); break;
  }
}

static void menuScreen(const ScreenPoint& sp, bool touched) {
  static bool drawn = false;

  static Button newBtn;
  static Button contactsBtn;
  static Button refreshBtn;
  static Button recentsBtn;

  if (!drawn) {
    tft.fillScreen(ILI9341_BLACK);
    tft.setCursor(0, 0);
    tft.setTextColor(ILI9341_WHITE);
    tft.setTextSize(2);

    tft.println("     Messages");
    tft.println("");

    newBtn.initButton(20, 60, 200, 40, "New");
    contactsBtn.initButton(20, 110, 200, 40, "Contacts");
    refreshBtn.initButton(20, 160, 200, 40, "Refresh");
    recentsBtn.initButton(20, 210, 200, 40, "Recents");

    drawn = true;
  }

  if (!tapEdge(touched)) return;

  if (newBtn.isClicked(sp)) {
    tft.fillScreen(ILI9341_BLACK);
    tft.setCursor(0,0);
    uiScreen = UI_NEWTEXT;
    drawn = false;
    return;
  }

  if (contactsBtn.isClicked(sp)) {
    tft.fillScreen(ILI9341_BLACK);
    tft.setCursor(0,0);
    contactsMenu();     // if this becomes a screen later, route it like recents

    drawn = false;
    return;
  }

  if (refreshBtn.isClicked(sp)) {
    tft.fillScreen(ILI9341_BLACK);
    tft.setCursor(0, 0);
    receive();
    drawn = false;
    return;
  }

  if (recentsBtn.isClicked(sp)) {
    tft.fillScreen(ILI9341_BLACK);
    tft.setCursor(0, 0);
    uiScreen = UI_RECENTS;
    drawn = false;      // so menu redraws when we come back
    return;
  }
}

static void textScreen(const ScreenPoint& sp, bool touched) {
  static bool drawn = false;
  static bool gotNumber = false;
  static bool startedText = false;
  static char remoteNum[20];   // static so it persists

  if (!drawn) {
    tft.fillScreen(ILI9341_BLACK);
    tft.setTextColor(ILI9341_WHITE);
    tft.setTextSize(2);

    drawBackButton();
    tft.setCursor(50, 10);
    tft.print("New Message");

    tft.setCursor(0, 50);
    tft.print("To: ");

    // reset state when entering screen
    gotNumber = false;
    startedText = false;
    remoteNum[0] = '\0';

    drawn = true;
  }

  // Back button (edge-detected)
  if (!tapEdge(touched)) return;

  if (backClicked(sp)) {
    drawn = false;
    uiScreen = UI_MENU;
    return;
  }

  // Read the number ONCE from Serial, then display it
  if (!gotNumber) {
    // If your readSerial is blocking, that's fine for now.
    // If it's non-blocking, you may want it to return >0 only when a full line is received.
    if (readSerial(remoteNum) > 0) {
      gotNumber = true;

      // show the entered number
      tft.setCursor(0, 70);
      tft.print(remoteNum);
    }
    return; // wait until we have a number
  }

  // Start the text flow ONCE
  if (!startedText) {
    startedText = true;

    // If text(remoteNum) takes over the UI and returns later, this is fine.
    // If text() is blocking and uses Serial to collect message body, also fine for now.
    text(remoteNum);
    // When text() returns, you might want to go back to menu or stay here.
    // For now, go back to menu:
    drawn = false;
    uiScreen = UI_MENU;
    return;
  }
}


static void drawConversationToTFT(int selection) {
  int idx = threadTop - (selection - 1);
  if (idx < 0 || idx > threadTop) return;

  MessageThread &t = threads[idx];

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
}

static void recentsScreen(const ScreenPoint& sp, bool touched) {
  static bool drawn = false;
  static Button threadBtns[MAX_CONVERSATIONS];

  const int listStartY = 50;
  const int rowH = 34;
  const int x = 10;
  const int w = 220;

  if (!drawn) {
    tft.fillScreen(ILI9341_BLACK);
    tft.setTextColor(ILI9341_WHITE);
    tft.setTextSize(2);

    drawBackButton();
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

  if (!tapEdge(touched)) return;

  if (backClicked(sp)) {
    drawn = false;
    uiScreen = UI_MENU;
    return;
  }

  for (int rowIndex = 0; rowIndex <= threadTop; rowIndex++) {
    if (threadBtns[rowIndex].isClicked(sp)) {
      convoSelection = rowIndex + 1; // 1..N
      drawn = false;
      uiScreen = UI_CONVO;
      return;
    }
  }
}


static void convoScreen(const ScreenPoint& sp, bool touched) {
  static bool drawn = false;

  if (!drawn) {
    tft.fillScreen(ILI9341_BLACK);
    tft.setTextColor(ILI9341_WHITE);
    tft.setTextSize(2);

    drawBackButton();
    drawConversationToTFT(convoSelection);

    drawn = true;
  }

  if (!tapEdge(touched)) return;

  if (backClicked(sp)) {
    drawn = false;
    uiScreen = UI_RECENTS;
    return;
  }
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





// Main menu UI
/*void messagesMenu(const ScreenPoint& sp, bool touched) {
  // Persist across calls (loop calls messagesMenu repeatedly)
  static bool drawn = false;

  // Debounce: fire only once per touch (on touch-down edge)
  static bool wasTouched = false;

  // Buttons persist
  static Button newBtn;
  static Button contactsBtn;
  static Button refreshBtn;
  static Button recentsBtn;

  // Draw UI once (or after returning from another action)
  if (!drawn) {
    tft.fillScreen(ILI9341_BLACK);
    tft.setCursor(0, 0);
    tft.setTextColor(ILI9341_WHITE);
    tft.setTextSize(2);

    tft.println("     Messages");
    tft.println("");

    // 4 main actions
    newBtn.initButton(20, 60, 200, 40, "New");
    contactsBtn.initButton(20, 110, 200, 40, "Contacts");
    refreshBtn.initButton(20, 160, 200, 40, "Refresh");
    recentsBtn.initButton(20, 210, 200, 40, "Recents");

    // Recents list below buttons (adjust Y if needed)
    // tft.setCursor(0, 220);
    tft.setTextSize(2);
    tft.setTextColor(ILI9341_WHITE);

    drawn = true;
  }

  // Debounce / edge-detect touch:
  // - when not touched: reset latch
  // - when touched continuously: ignore repeats
  if (!touched) {
    wasTouched = false;
    return;
  }
  if (wasTouched) return;
  wasTouched = true;

  // Handle tap
  if (newBtn.isClicked(sp)) {
    tft.fillScreen(ILI9341_ORANGE);
    tft.setCursor(0, 0);
    tft.setTextColor(ILI9341_WHITE);
    tft.setTextSize(2);

    //text();       // your existing function
    drawn = false; // force redraw next time we come back
    return;
  }

  if (contactsBtn.isClicked(sp)) { // Compares button x/y position with touch point 
    tft.fillScreen(ILI9341_GREEN);
    tft.setCursor(0, 0);
    tft.setTextColor(ILI9341_WHITE);
    tft.setTextSize(2);

    //contactsMenu(); // your existing function
    drawn = false;
    return;
  }

  if (refreshBtn.isClicked(sp)) {
    tft.fillScreen(ILI9341_BLACK);
    tft.setCursor(0, 0);
    tft.setTextColor(ILI9341_WHITE);
    tft.setTextSize(2);

    //receive();    // your existing function
    drawn = false;
    return;
  }

  if (recentsBtn.isClicked(sp)) {
    tft.fillScreen(ILI9341_RED);
    tft.setCursor(0, 0);
    tft.setTextColor(ILI9341_WHITE);
    tft.setTextSize(2);

   //  viewRecents(sp,wasTouched);   // existing function
    drawn = false;
    return;
  }

}



// Prints the "recents" list (threads) with 1-based numbering.
void viewRecents(const ScreenPoint& sp, bool touched) {
  static bool drawn = false;
  static bool wasTouched = false;

  // One button per possible thread
  static Button threadBtns[MAX_CONVERSATIONS];

  const int headerY = 0;
  const int listStartY = 60;
  const int rowH = 34;
  const int x = 10;
  const int w = 220;

  if (!drawn) {
    tft.fillScreen(ILI9341_BLACK);
    tft.setTextColor(ILI9341_WHITE);
    tft.setTextSize(2);

    tft.setCursor(0, headerY);
    tft.println("Messages:");

    // Draw each recent thread as its own button row
    for (int i = threadTop; i >= 0; i--) {
      int selection = (threadTop - i + 1);          // 1 = most recent
      int rowIndex  = selection - 1;                // 0-based row index
      int y = listStartY + rowIndex * rowH;

      // Label = contact name or phone number
      char label[24];
      int cidx = findContactName(threads[i].phoneNumber);
      if (cidx != -1) {
        strncpy(label, contactList[cidx].name, sizeof(label)-1);
        label[sizeof(label)-1] = '\0';
      } else {
        strncpy(label, threads[i].phoneNumber, sizeof(label)-1);
        label[sizeof(label)-1] = '\0';
      }

      // Make the whole row clickable
      // NOTE: if your Button uses CENTER coords, use (x + w/2, y + rowH/2)
      threadBtns[rowIndex].initButton(x, y, w, rowH, label);

      // Optional: draw the numbering on the left
      tft.setCursor(0, y + 8);
      tft.print(selection);
      tft.print(".");
    }

    drawn = true;
  }

  // --- edge-detect tap ---
  if (!touched) { wasTouched = false; return; }
  if (wasTouched) return;
  wasTouched = true;

  // Check which row was clicked
  for (int rowIndex = 0; rowIndex <= threadTop; rowIndex++) {
    if (threadBtns[rowIndex].isClicked(sp)) {
      int selection = rowIndex + 1;   // 1..N
      openConversation(selection);
      // Switch to conversation screen here 
      return;
    }
  }
}

*/

// You declared convo() in the new .h, but it's unclear what it should do now.
// Keeping it as a stub so you don't get a linker error if something calls it.
void convo() {
  // TODO: Define what convo() should mean, or delete it from Messages.h
  // If you meant "print a conversation", you now have openConversation(selection).
}
