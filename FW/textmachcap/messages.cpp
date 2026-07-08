#include "messages.h"
#include "types.h"
#include "buttons.h"
#include "contacts.h" // for normalizing phone # 
#include <SD.h>

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

static msgButton convoMsgBtns[MAX_MESSAGES_PER_CONVO];  // one bubble per message in the open thread
static Button convoScrollUpBtn;
static Button convoScrollDownBtn;
static int convoScrollOffset = 0;   // messages scrolled back from the newest (0 = showing newest at bottom)
static int convoVisibleTop   = 0;   // oldest message index currently drawn on screen

static bool recentMessagesDrawn = false;
static bool conversationDrawn   = false;

// -------------------------------------------------------------------------------------------------
// Private helpers
// -------------------------------------------------------------------------------------------------

static int  findThreadByPhone(const char* phone);
static void moveThreadToTop(int idx);
static void copyBounded(char* dst, const char* src, size_t dstSize);

// -------------------------------------------------------------------------------------------------
// Phone normalization helper
// -------------------------------------------------------------------------------------------------

void normalizePhoneNumber(const char* input, char* output, int outLen){
  int i = 0;
  int d = 0;
  if(input[0] == '+'){
i++;
  } 

  char digits[20] = {0};

// copy only digits over
  for (; input[i] && d < 19; i++) {
      if (isdigit(input[i])) digits[d++] = input[i];
  }

// 
digits[d] = '\0';

  if(d == 10) {
    // Add +1
    snprintf(output,outLen,"+1%s", digits);
  }else if(d == 11 && digits[0] == '1'){
    // Add +
    snprintf(output,outLen,"+%s", digits);
  } else {
        // short code or international — leave as-is with +
        snprintf(output, outLen, "+%s", digits);
    }


}

// -------------------------------------------------------------------------------------------------
// Message wrapping / sizing helpers (for msgButton in buttons.h)
// -------------------------------------------------------------------------------------------------

// Word-wraps body against maxWidth (pixels, using the TFT's currently set font/size).
// If outLines is non-null, copies up to maxOutLines wrapped line strings into it
// (each bounded to MAX_BODY_LEN chars) — this is what msgButton::render() uses to
// actually print a bubble's text. Always returns the total line count, even if it
// exceeds maxOutLines.
int wrapMessageText(const char* body, int maxWidth, char outLines[][MAX_BODY_LEN], int maxOutLines) {
  if (!body || body[0] == '\0') {
    if (outLines && maxOutLines > 0) outLines[0][0] = '\0';
    return 1;
  }

  int16_t x1, y1;
  uint16_t w, h;

  int lines = 0;
  char lineBuf[MAX_BODY_LEN] = { 0 };
  int lineLen = 0;

  const char* p = body;
  while (*p) {
    const char* wordStart = p;
    while (*p && *p != ' ') p++;
    int wordLen = p - wordStart;
    if (wordLen >= MAX_BODY_LEN) wordLen = MAX_BODY_LEN - 1;

    char candidate[MAX_BODY_LEN];
    int candLen = 0;
    if (lineLen > 0) {
      memcpy(candidate, lineBuf, lineLen);
      candLen = lineLen;
      candidate[candLen++] = ' ';
    }
    if (candLen + wordLen >= MAX_BODY_LEN) wordLen = MAX_BODY_LEN - candLen - 1;
    memcpy(candidate + candLen, wordStart, wordLen);
    candLen += wordLen;
    candidate[candLen] = '\0';

    tft.getTextBounds(candidate, 0, 0, &x1, &y1, &w, &h);

    if ((int)w > maxWidth && lineLen > 0) {
      // doesn't fit alongside current line — commit it and start the word on a fresh line
      if (outLines && lines < maxOutLines) copyBounded(outLines[lines], lineBuf, MAX_BODY_LEN);
      lines++;

      memcpy(lineBuf, wordStart, wordLen);
      lineLen = wordLen;
      lineBuf[lineLen] = '\0';
    } else {
      memcpy(lineBuf, candidate, candLen);
      lineLen = candLen;
      lineBuf[lineLen] = '\0';
    }

    if (*p == ' ') p++;  // skip the space between words
  }

  // commit whatever's left on the final line
  if (outLines && lines < maxOutLines) copyBounded(outLines[lines], lineBuf, MAX_BODY_LEN);
  lines++;

  return lines;
}

// Returns how many word-wrapped lines `body` takes up at maxWidth — used to size a
// msgButton bubble vertically before it's drawn.
int measureMessageLines(const char* body, int maxWidth) {
  return wrapMessageText(body, maxWidth, nullptr, 0);
}

void saveMessageToSD(const char* phone, Msg& msg) {
    if (msg.saved) return;

    char filename[40];
    char normalized[MAX_PHONE_LEN];
    normalizePhoneNumber(phone, normalized, MAX_PHONE_LEN);

    // SD's short-filename (8.3) format only allows 8 chars before the extension,
    // but a normalized number is 10-11 digits — so key the file on just the last
    // 8. loadMessagesFromSD() doesn't rely on the filename for the phone number
    // (it re-reads the full number from each line's content), so this is safe.
    const char* digits = normalized + 1;
    int digitsLen = strlen(digits);
    const char* shortName = (digitsLen > 8) ? digits + (digitsLen - 8) : digits;
    snprintf(filename, sizeof(filename), "/msg/%s.csv", shortName);

    Serial.print("Attempting: ");
    Serial.println(filename);

    if (!SD.exists("/msg")) SD.mkdir("/msg");

    File f = SD.open(filename, FILE_WRITE);
    if (!f) {
        Serial.println("Failed to open message file");
        return;
    }

    f.print(normalized);     f.print("|");
    f.print(msg.body);       f.print("|");
    f.print(msg.timestamp);  f.print("|");
    f.println(msg.dir == IN ? "IN" : "OUT");
    f.close();

    msg.saved = true;
    Serial.print("Saved to: ");
    Serial.println(filename);
}

void loadMessagesFromSD() {
    if (!SD.exists("/msg")) {
        Serial.println("No /msg directory found");
        return;
    }

    File dir = SD.open("/msg");
    if (!dir) {
        Serial.println("Failed to open /msg");
        return;
    }

    while (true) {
        File entry = dir.openNextFile();
        if (!entry) break;  // no more files

        // skip directories
        if (entry.isDirectory()) {
            entry.close();
            continue;
        }

        // reconstruct phone number from filename
        // filename = "14156100909.csv" → phone = "+14156100909"
        char phone[MAX_PHONE_LEN];
        const char* fname = entry.name();
        char* dot = strrchr(fname, '.');
        int nameLen = dot ? (dot - fname) : strlen(fname);
        phone[0] = '+';
        strncpy(phone + 1, fname, nameLen);
        phone[nameLen + 1] = '\0';

        Serial.print("Loading: ");
        Serial.println(phone);

        while (entry.available()) {
            String line = entry.readStringUntil('\n');
            line.trim();
            if (line.length() == 0) continue;

            // format: normalized|body|timestamp|dir
            int sep1 = line.indexOf('|');
            int sep2 = line.indexOf('|', sep1 + 1);
            int sep3 = line.lastIndexOf('|');

            if (sep1 == -1 || sep2 == -1 || sep3 == -1 || sep3 == sep2) continue;

            String storedPhone = line.substring(0, sep1);
            String body        = line.substring(sep1 + 1, sep2);
            String timestamp   = line.substring(sep2 + 1, sep3);
            String dirStr      = line.substring(sep3 + 1);

            MsgDir dir = (dirStr == "OUT") ? OUT : IN;

            char bodyBuf[MAX_BODY_LEN];
            char tsBuf[MAX_TIMESTAMP_LEN];
            char phoneBuf[MAX_PHONE_LEN];

            copyBounded(bodyBuf,  body.c_str(),         MAX_BODY_LEN);
            copyBounded(tsBuf,    timestamp.c_str(),    MAX_TIMESTAMP_LEN);
            copyBounded(phoneBuf, storedPhone.c_str(),  MAX_PHONE_LEN);

            // push into RAM — but mark as already saved so it doesn't write back to SD
            int idx = findThreadByPhone(phoneBuf);
            if (idx == -1) {
                if (threadTop + 1 >= MAX_CONVERSATIONS) continue;
                threadTop++;
                idx = threadTop;
                copyBounded(threads[idx].phoneNumber, phoneBuf, MAX_PHONE_LEN);
                threads[idx].lastMessageIndex = -1;
            }

            MessageThread& t = threads[idx];
            if (t.lastMessageIndex + 1 < MAX_MESSAGES_PER_CONVO) {
                t.lastMessageIndex++;
                copyBounded(t.messages[t.lastMessageIndex].body,      bodyBuf, MAX_BODY_LEN);
                copyBounded(t.messages[t.lastMessageIndex].timestamp, tsBuf,   MAX_TIMESTAMP_LEN);
                t.messages[t.lastMessageIndex].dir   = dir;
                t.messages[t.lastMessageIndex].saved = true;  // ← already on SD, don't resave
            }
        }
        entry.close();
    }
    dir.close();
    Serial.println("Messages loaded from SD");
}

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

bool drawConversationToTFT(int selection, const ScreenPoint& sp, bool justPressed) {
  const int msgStartY = 200; // start here then print each message in a interactable box going upwards. 
  const int headerEndY = 50; // Below the header will be the messages 
  


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

    // tft.println("----------------");

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
    t.messages[t.lastMessageIndex].saved = false;

    // copy the individual message into the log of messages for this thread including its body, timestamp, and direction
    copyBounded(t.messages[t.lastMessageIndex].body, text, MAX_BODY_LEN);
    copyBounded(threads[idx].messages[t.lastMessageIndex].timestamp, time, MAX_TIMESTAMP_LEN);
    t.messages[t.lastMessageIndex].dir = dir;

  

    // copy message into a file of a specific phone number with all msgs in conversation

    saveMessageToSD(phone, t.messages[t.lastMessageIndex]);

  } else {
    // TODO
  }

  // Most recently active thread moves to top
  moveThreadToTop(idx);

}
