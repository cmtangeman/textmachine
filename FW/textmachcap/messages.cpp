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
static Button convoReplyBtn;        // "Text" — jumps to compose with this thread's number prefilled
static int convoScrollOffset  = 0;  // messages scrolled back from the newest (0 = showing newest at bottom)
static int convoVisibleTop    = 0;  // oldest message index currently drawn on screen
static int convoVisibleBottom = -1; // newest message index currently drawn on screen

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
                t.messages[t.lastMessageIndex].kept = true;
            }
        }
        entry.close();
    }
    dir.close();
    Serial.println("Messages loaded from SD");
}

void unsaveMessageFromSD(const char* phone, int msgIdx) {
    char filename[40];
    char normalized[MAX_PHONE_LEN];
    normalizePhoneNumber(phone, normalized, MAX_PHONE_LEN);

    const char* digits = normalized + 1;
    int digitsLen = strlen(digits);
    const char* shortName = (digitsLen > 8) ? digits + (digitsLen - 8) : digits;
    snprintf(filename, sizeof(filename), "/msg/%s.csv", shortName);

    // ── Read all lines from file ───────────────────────────
    String lines[MAX_MESSAGES_PER_CONVO];
    int lineCount = 0;

    File f = SD.open(filename, FILE_READ);
    if (!f) {
        Serial.println("unsave: file not found");
        return;
    }

    while (f.available() && lineCount < MAX_MESSAGES_PER_CONVO) {
        String line = f.readStringUntil('\n');
        line.trim();
        if (line.length() > 0) {
            lines[lineCount++] = line;
        }
    }
    f.close();

    // ── Find which line corresponds to msgIdx ──────────────
    // lines are in chronological order, msgIdx maps directly
    if (msgIdx < 0 || msgIdx >= lineCount) {
        Serial.println("unsave: invalid index");
        return;
    }

    // ── Rewrite file skipping that line ───────────────────
    SD.remove(filename);
    File out = SD.open(filename, FILE_WRITE);
    if (!out) {
        Serial.println("unsave: failed to rewrite file");
        return;
    }

    for (int i = 0; i < lineCount; i++) {
        if (i == msgIdx) continue;  // skip the unsaved message
        out.println(lines[i]);
    }
    out.close();

    Serial.print("Unsaved message ");
    Serial.print(msgIdx);
    Serial.print(" from ");
    Serial.println(filename);
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

bool convoReplyBtnPressed(const ScreenPoint& sp) {
  return convoReplyBtn.isClicked(sp);
}

const char* getConversationPhone(int selection) {
  int idx = threadTop - selection;
  if (idx < 0 || idx > threadTop) return nullptr;
  return threads[idx].phoneNumber;
}

void recentMessagesReset() {
  recentMessagesDrawn = false;
}

void conversationReset() {
  conversationDrawn = false;
  convoScrollOffset = 0;  // always reopen a thread scrolled to the newest message
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
  const int headerEndY     = 50;    // back button + contact name — stays fixed, never scrolls
  const int msgRegionTop   = headerEndY;
  const int msgRegionBottom = 200;  // newest bubble's bottom edge lands here
  const int replyBtnY      = msgRegionBottom + 10;
  const int replyBtnH      = 40;

  const int bubbleMarginX = 10;
  const int scrollColW    = 30;     // reserved right-hand column for scroll buttons, mirrors contactsScreen
  const int bubbleWidth   = tft.width() - bubbleMarginX * 2 - scrollColW;
  const int bubblePadding = 6;
  const int lineHeight    = 10;
  const int bubbleGapY    = 6;

  int idx = threadTop - selection;
  if (idx < 0 || idx > threadTop) return false;

  MessageThread& t = threads[idx];

  if (!conversationDrawn) {
    tft.fillScreen(ILI9341_BLACK);
    tft.setTextColor(ILI9341_WHITE);
    tft.setTextSize(2);

    convoBackBtn.initButton(0, 0, 30, 30, "<");

    tft.setCursor(50, 10);
    int cidx = findContactName(t.phoneNumber);
    if (cidx != -1) {
      tft.print(contactList[cidx].name);
    } else {
      tft.print(t.phoneNumber);
    }

    // ── Lay out bubbles bottom-up: newest at msgRegionBottom, older ones stack upward ──
    int newestIdx = t.lastMessageIndex - convoScrollOffset;
    if (newestIdx > t.lastMessageIndex) newestIdx = t.lastMessageIndex;

    int y = msgRegionBottom;
    int oldestVisible = newestIdx + 1;  // decremented as messages are fit in below

    for (int i = newestIdx; i >= 0; i--) {
      int lines = measureMessageLines(t.messages[i].body, bubbleWidth - 2 * bubblePadding);
      int h = lines * lineHeight + 2 * bubblePadding;

      if (y - h < msgRegionTop) break;  // no more room — rest is reachable only by scrolling

      y -= h;
      convoMsgBtns[i].initMsgButton(bubbleMarginX, y, bubbleWidth, h,
                                    t.messages[i].body, t.messages[i].dir, t.messages[i].kept);
      oldestVisible = i;
      y -= bubbleGapY;
    }

    convoVisibleTop    = oldestVisible;
    convoVisibleBottom = newestIdx;

    // ── Scroll buttons — same up/down-by-one pattern as contactsScreen ──
    if (convoVisibleTop > 0) {
      convoScrollUpBtn.initButton(tft.width() - scrollColW, msgRegionTop, scrollColW, 30, "^");
    }
    if (convoScrollOffset > 0) {
      convoScrollDownBtn.initButton(tft.width() - scrollColW, msgRegionBottom - 30, scrollColW, 30, "v");
    }

    // ── Reply button — jumps to compose with this thread's number prefilled ──
    convoReplyBtn.initButton(bubbleMarginX, replyBtnY, tft.width() - 2 * bubbleMarginX, replyBtnH, "Text");

    conversationDrawn = true;
  }

  if (!justPressed) return true;

  if (convoVisibleTop > 0 && convoScrollUpBtn.isClicked(sp)) {
    convoScrollOffset++;
    conversationDrawn = false;
    return true;
  }

  if (convoScrollOffset > 0 && convoScrollDownBtn.isClicked(sp)) {
    convoScrollOffset--;
    conversationDrawn = false;
    return true;
  }

  // ── Tap a bubble to toggle its "kept" indicator ──
  for (int i = convoVisibleTop; i <= convoVisibleBottom; i++) {

    if (convoMsgBtns[i].isClicked(sp)) {
        t.messages[i].kept = !t.messages[i].kept;
        
        if (t.messages[i].kept) {
            saveMessageToSD(t.phoneNumber, t.messages[i]);  // ← fixed index
        } else {
            unsaveMessageFromSD(t.phoneNumber, i);
            t.messages[i].saved = false;
        }
        
        conversationDrawn = false;
        return true;
    }
    
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

    // saveMessageToSD(phone, t.messages[t.lastMessageIndex]);

  } else {
    // TODO
  }

  // Most recently active thread moves to top
  moveThreadToTop(idx);

}
