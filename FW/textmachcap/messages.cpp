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
static Button recentScrollUpBtn;
static Button recentScrollDownBtn;
static int recentScrollOffset = 0;   // threads scrolled past from the top (0 = showing newest first)
static const int MAX_VISIBLE_RECENTS = 6;

static msgButton convoMsgBtns[MAX_MESSAGES_PER_CONVO];  // one bubble per message in the open thread
static Button convoScrollUpBtn;
static Button convoScrollDownBtn;
static Button convoReplyBtn;        // reply bar — styled like the compose Msg field; tapping it drops into the keyboard with this thread's number prefilled
static Button convoRxBtn;           // top-right RX button — checks for new messages without leaving this conversation
static int convoScrollOffset  = 0;  // messages scrolled back from the newest (0 = showing newest at bottom)
static int convoVisibleTop    = 0;  // oldest message index currently drawn on screen
static int convoVisibleBottom = -1; // newest message index currently drawn on screen

// Hold-a-bubble-2-seconds-to-flip-it state (double-tap turned out
// unreliable on the actual touch hardware — a sustained hold is much
// easier to detect cleanly than two discrete taps in a tight window).
// Unlike double-tap, a hold CAN be cleanly told apart from an ordinary
// short tap before deciding what to do: the "kept" toggle now fires on
// RELEASE rather than on touch-down, only if the press never crossed the
// long-press threshold — see the tap/hold handling in
// drawConversationToTFT() below, which (unlike the rest of this file's
// touch handling) has to run every frame, not just on justPressed, since
// noticing a hold means watching the touch persist across frames.
static int convoFlippedIdx   = -1;  // message index currently showing its flipped "back" (just the time), -1 = none
static int convoPressIdx     = -1;  // message index the in-progress touch started on, -1 = none/not on a bubble
static unsigned long convoPressStartMillis = 0;
static bool convoLongPressFired = false;  // so crossing the threshold flips exactly once per hold, not every frame past it
static const unsigned long CONVO_LONG_PRESS_MS = 2000;

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

// -------------------------------------------------------------------------------------------------
// Timestamp comparison
// -------------------------------------------------------------------------------------------------

// Weekday index matching formatTimestamp()'s own days[] table (UI.cpp) —
// 0=Sat,1=Sun,2=Mon,3=Tue,4=Wed,5=Thu,6=Fri (Zeller's congruence order, not
// alphabetical or Sun-first). Returns -1 if `raw` doesn't start with a
// recognized 3-letter abbreviation.
static int weekdayIndex(const char* raw) {
    static const char* days[] = {"Sat","Sun","Mon","Tue","Wed","Thu","Fri"};
    for (int i = 0; i < 7; i++) {
        if (strncmp(raw, days[i], 3) == 0) return i;
    }
    return -1;
}

// Zeller's congruence — same convention as weekdayIndex() (0=Sat..6=Fri).
// Duplicated from UI.cpp's formatTimestamp() (not shared via a header,
// same reasoning as daysFromCivil() below) since this file needs it to
// anchor the weekday-only display shape to an actual date.
static int zellerWeekday(int y, int m, int d) {
    if (m < 3) { m += 12; y--; }
    int k = y % 100, j = y / 100;
    return (d + (13*(m+1))/5 + k + k/4 + j/4 - 2*j) % 7;
}

// Days since 1970-01-01 (Howard Hinnant's days_from_civil) — duplicated
// from UI.cpp's formatTimestamp(), which has the full explanation. Kept as
// a separate copy rather than shared through a header since both this file
// and UI.cpp already parse timestamps independently and neither owns a
// general "date math" module — this and zellerWeekday() above are the only
// two pieces that would move, and they're tiny/self-contained.
static long daysFromCivil(int y, int m, int d) {
    y -= m <= 2;
    long era = (y >= 0 ? y : y - 399) / 400;
    unsigned yoe = (unsigned)(y - era * 400);
    unsigned doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    unsigned doe = yoe * 365 + yoe/4 - yoe/100 + doy;
    return era * 146097 + (long)doe - 719468;
}

// Parses the raw AT+CCLK/CMGL modem shape ("yy/MM/dd,hh:mm:ss±zz") just
// far enough to get the date — same fields UI.cpp's parseRawTimestamp()
// reads, needed here only to turn `nowRaw` (see compareTimestamps() below)
// into an actual reference date.
static bool parseRawModemDate(const char* raw, int* year, int* month, int* day) {
    if (!raw || strlen(raw) < 8) return false;
    *year  = 2000 + (raw[0]-'0')*10 + (raw[1]-'0');
    *month = (raw[3]-'0')*10 + (raw[4]-'0');
    *day   = (raw[6]-'0')*10 + (raw[7]-'0');
    return true;
}

// Resolves either of formatTimestamp()'s two display shapes into an
// absolute day count (days-since-epoch) plus time-of-day, so a "Wed 14:32"
// and a "9/5/2026" can be ranked against each other on equal footing —
// each is only ever ONE of these two representations of a real timestamp,
// picked by age at format time, not both at once, so recovering a
// comparable point in time means handling whichever one is actually there:
//   "Www HH:MM" — weekday + time, no date of its own; anchored to the most
//                 recent day matching that weekday, relative to
//                 (refYear, refMonth, refDay) — i.e. "now".
//   "M/D/YYYY"  — an actual date, no time (treated as midnight).
// Returns false for anything matching neither shape (e.g. "Unknown").
static bool resolveTimestamp(const char* raw, int refYear, int refMonth, int refDay,
                              long* dayCount, int* hour, int* minute) {
    if (!raw) return false;
    size_t len = strlen(raw);

    // "Www HH:MM" — always exactly 9 chars (%02d zero-pads hour/minute).
    if (len == 9 && raw[3] == ' ' && raw[6] == ':') {
        int wd = weekdayIndex(raw);
        if (wd == -1) return false;
        if (!isdigit((unsigned char)raw[4]) || !isdigit((unsigned char)raw[5]) ||
            !isdigit((unsigned char)raw[7]) || !isdigit((unsigned char)raw[8])) {
            return false;
        }
        int h = (raw[4]-'0')*10 + (raw[5]-'0');
        int m = (raw[7]-'0')*10 + (raw[8]-'0');
        if (h > 23 || m > 59) return false;

        int today   = zellerWeekday(refYear, refMonth, refDay);
        int daysAgo = (today - wd + 7) % 7;
        *dayCount = daysFromCivil(refYear, refMonth, refDay) - daysAgo;
        *hour = h;
        *minute = m;
        return true;
    }

    // "M/D/YYYY" — 1-2 digit month, 1-2 digit day, 4-digit year, no
    // leading zeros (matches formatTimestamp()'s snprintf("%d/%d/%d", ...)).
    int mo = 0, d = 0, y = 0;
    const char* p = raw;
    if (!isdigit((unsigned char)*p)) return false;
    while (isdigit((unsigned char)*p)) { mo = mo * 10 + (*p - '0'); p++; }
    if (*p != '/' || mo < 1 || mo > 12) return false;
    p++;
    if (!isdigit((unsigned char)*p)) return false;
    while (isdigit((unsigned char)*p)) { d = d * 10 + (*p - '0'); p++; }
    if (*p != '/' || d < 1 || d > 31) return false;
    p++;
    if (!isdigit((unsigned char)*p)) return false;
    while (isdigit((unsigned char)*p)) { y = y * 10 + (*p - '0'); p++; }
    if (*p != '\0' || y < 1970) return false;

    *dayCount = daysFromCivil(y, mo, d);
    *hour = 0;
    *minute = 0;  // date-only shape carries no time
    return true;
}

int compareTimestamps(const char* a, const char* b, const char* nowRaw) {
    int refYear, refMonth, refDay;
    bool haveRef = parseRawModemDate(nowRaw, &refYear, &refMonth, &refDay);

    long aDays = 0, bDays = 0;
    int ah = 0, am = 0, bh = 0, bm = 0;
    bool aOk = haveRef && resolveTimestamp(a, refYear, refMonth, refDay, &aDays, &ah, &am);
    bool bOk = haveRef && resolveTimestamp(b, refYear, refMonth, refDay, &bDays, &bh, &bm);

    if (!aOk && !bOk) return 0;
    if (!aOk) return -1;  // unparseable (or no reference date to anchor against) sorts oldest
    if (!bOk) return 1;

    if (aDays != bDays) return (aDays > bDays) ? 1 : -1;
    if (ah != bh) return (ah > bh) ? 1 : -1;
    if (am != bm) return (am > bm) ? 1 : -1;
    return 0;
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
            // Also need to mark as read
            int idx = findThreadByPhone(phoneBuf);
            if (idx == -1) {
                if (threadTop + 1 >= MAX_CONVERSATIONS) continue;
                threadTop++;
                idx = threadTop;
                copyBounded(threads[idx].phoneNumber, phoneBuf, MAX_PHONE_LEN);
                threads[idx].lastMessageIndex = -1;
                threads[idx].readThread = true;
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

    // SD.openNextFile() hands threads back in whatever order the filesystem
    // happens to store them — no relation to actual recency. Live operation
    // always keeps the most-recently-active thread at threadTop
    // (pushMessage() -> moveThreadToTop() on every new message), so bring a
    // fresh SD load in line with that same invariant: find whichever loaded
    // thread's own most recent message (messages[lastMessageIndex], the
    // last one appended per-file above) has the latest timestamp, and let
    // that thread's idx become threadTop.
    if (threadTop >= 0) {
        // compareTimestamps() needs the RAW modem format here (to get an
        // actual reference date), not formatTimestamp()'s output — see
        // messages.h. If the modem doesn't answer, pass "" so every
        // comparison below just falls through as "equal" (no reordering)
        // instead of misusing a display-formatted string as if it were raw.
        char now[32];
        bool haveNow = getCurrentRawTimestamp(now, sizeof(now));

        int newestIdx = 0;
        for (int i = 1; i <= threadTop; i++) {
            const char* a = threads[i].messages[threads[i].lastMessageIndex].timestamp;
            const char* b = threads[newestIdx].messages[threads[newestIdx].lastMessageIndex].timestamp;
            if (compareTimestamps(a, b, haveNow ? now : "") > 0) {
                newestIdx = i;
            }
        }
        moveThreadToTop(newestIdx);
    }

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

bool convoRxBtnPressed(const ScreenPoint& sp) {
  return convoRxBtn.isClicked(sp);
}

const char* getConversationPhone(int selection) {
  int idx = threadTop - selection;
  if (idx < 0 || idx > threadTop) return nullptr;
  return threads[idx].phoneNumber;
}

int getConversationSelectionByPhone(const char* phone) {
  int idx = findThreadByPhone(phone);
  if (idx == -1) return -1;
  return threadTop - idx;
}

int getConversationMessageCount(const char* phone) {
  int idx = findThreadByPhone(phone);
  if (idx == -1) return 0;
  return threads[idx].lastMessageIndex + 1;
}

void recentMessagesReset() {
  recentMessagesDrawn = false;
  recentScrollOffset  = 0;  // always reopen Recents scrolled to the newest thread
}

void conversationReset() {
  conversationDrawn = false;
  convoScrollOffset = 0;  // always reopen a thread scrolled to the newest message
  convoFlippedIdx = -1;
  convoPressIdx = -1;
}

// -------------------------------------------------------------------------------------------------
// Recent messages screen
// -------------------------------------------------------------------------------------------------

int recentMessagesScreen(const ScreenPoint& sp, bool justPressed) {
  const int listStartY = 56;
  const int rowH       = 42;
  const int x           = 10;
  const int w           = 194;   // leaves room for the scroll column
  const int scrollColX = 210;

  int totalThreads = threadTop + 1;
  // FIX: this used to draw every thread unconditionally with no scroll
  // clipping (unlike contactsScreen, which already had MAX_VISIBLE
  // pagination) — past ~8 threads it would overflow off the bottom of the
  // screen. Now paginated the same way contactsScreen is.
  int visibleCount = min(MAX_VISIBLE_RECENTS, totalThreads - recentScrollOffset);
  if (visibleCount < 0) visibleCount = 0;

  if (!recentMessagesDrawn) {
    tft.fillScreen(UI_BG);

    msgBackBtn.initButton(0, 0, 32, 32, "<");

    uiUseTitleFont();
    tft.setTextColor(UI_TEXT);
    tft.setCursor(40, 20);
    tft.print("Recents");
    tft.setFont(NULL);

    if (recentScrollOffset > 0) {
      recentScrollUpBtn.initButton(scrollColX, listStartY, 30, 30, "^");
    }
    if (recentScrollOffset + MAX_VISIBLE_RECENTS < totalThreads) {
      recentScrollDownBtn.initButton(scrollColX, listStartY + MAX_VISIBLE_RECENTS * rowH - 30, 30, 30, "v");
    }

    if (totalThreads == 0) {
      uiUseDefaultFont();
      tft.setTextColor(UI_TEXT_DIM);
      tft.setCursor(x, listStartY + 20);
      tft.print("No conversations yet");
      tft.setTextSize(2);
    }

    for (int i = 0; i < visibleCount; i++) {
      int orderFromTop = i + recentScrollOffset;   // 0 = most recent thread
      int threadIdx    = threadTop - orderFromTop;
      int y            = listStartY + i * rowH;

      MessageThread& t = threads[threadIdx];

      char label[24];
      int cidx = findContactName(t.phoneNumber);
      if (cidx != -1) {
        copyBounded(label, contactList[cidx].name, sizeof(label));  // safely copy the phone number onto the label.
      } else {
        copyBounded(label, t.phoneNumber, sizeof(label));
      }

      // Row background is a flat tap target — name/preview/timestamp drawn on top.
      threadBtns[i].initButton(x, y, w, rowH, "", UI_BG);

      uiUseButtonFont();
      tft.setTextColor(UI_TEXT);
      tft.setCursor(x, y + 16);
      tft.print(label);
      tft.setFont(NULL);

      tft.setTextSize(1);
      tft.setTextColor(UI_TEXT_DIM);
      if (t.lastMessageIndex >= 0) {
        tft.setCursor(x + 148, y + 6);
        tft.print(t.messages[t.lastMessageIndex].timestamp);

        char preview[28];
        copyBounded(preview, t.messages[t.lastMessageIndex].body, sizeof(preview));
        tft.setCursor(x, y + 28);
        tft.print(preview);

        if(t.readThread == false){
          tft.fillCircle(220, threadBtns[i].y + threadBtns[i].height/2, 4, UI_DANGER);
        }
      }

      tft.drawFastHLine(8, y + rowH, w - 8, UI_BORDER);
    }

    recentMessagesDrawn = true;
  }

  if (!justPressed) return -1;

  if (recentScrollOffset > 0 && recentScrollUpBtn.isClicked(sp)) {
    recentScrollOffset--;
    recentMessagesDrawn = false;
    return -1;
  }
  if (recentScrollOffset + MAX_VISIBLE_RECENTS < totalThreads && recentScrollDownBtn.isClicked(sp)) {
    recentScrollOffset++;
    recentMessagesDrawn = false;
    return -1;
  }

  for (int i = 0; i < visibleCount; i++) {
    if (threadBtns[i].isClicked(sp)) {
      convoSelection = i + recentScrollOffset;
      return convoSelection;
    }
  }

  return -1;
}

// -------------------------------------------------------------------------------------------------
// Conversation screen
// -------------------------------------------------------------------------------------------------

// Rebuilds the raw modem-format string ("yy/MM/dd,hh:mm:ss±zz") a message's
// packed rawYear/rawMonth/rawDay/rawHour/rawMinute fields encode, for
// handing to UI.cpp's formatFullTimestamp() (which takes that raw shape,
// same as formatTimestamp()). Caller must have already checked
// msg.hasRawTime.
static void reconstructRawTimestamp(const Msg& msg, char* out, size_t outSize) {
    snprintf(out, outSize, "%02d/%02d/%02d,%02d:%02d:00+00",
             msg.rawYear, msg.rawMonth, msg.rawDay, msg.rawHour, msg.rawMinute);
}

bool drawConversationToTFT(int selection, const ScreenPoint& sp, bool justPressed, bool touched) {
  const int headerEndY     = 50;    // back button + contact name — stays fixed, never scrolls
  const int msgRegionTop   = headerEndY;

  // Reply bar sits flush against the bottom edge, mirroring the compose
  // screen's Msg field — the message region fills the rest of the screen
  // above it instead of stopping short and leaving dead space.
  const int replyBarH      = 36;
  const int replyBarY      = tft.height() - replyBarH;
  const int msgRegionBottom = replyBarY - 6;  // newest bubble's bottom edge lands here

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
    t.readThread = true;  // FIX: nothing previously marked a thread read on open
    tft.fillScreen(UI_BG);

    convoBackBtn.initButton(0, 0, 32, 32, "<");

    // Top-right RX button — same size/corner treatment as the back button,
    // mirrored to the opposite corner, so checking for new messages doesn't
    // require backing all the way out to the main menu mid-conversation.
    convoRxBtn.initButton(tft.width() - 40, 0, 32, 32, "RX");

    uiUseTitleFont();
    tft.setTextColor(UI_TEXT);
    tft.setCursor(40, 20);
    int cidx = findContactName(t.phoneNumber);
    if (cidx != -1) {
      tft.print(contactList[cidx].name);
    } else {
      tft.print(t.phoneNumber);
    }
    tft.setFont(NULL);

    // ── Lay out bubbles bottom-up: newest at msgRegionBottom, older ones stack upward ──
    int newestIdx = t.lastMessageIndex - convoScrollOffset;
    if (newestIdx > t.lastMessageIndex) newestIdx = t.lastMessageIndex;

    int y = msgRegionBottom;
    int oldestVisible = newestIdx + 1;  // decremented as messages are fit in below

    for (int i = newestIdx; i >= 0; i--) {
      // Bubble size is always based on the body text, whether or not this
      // bubble is currently flipped — otherwise flipping one bubble would
      // resize it and shove every other bubble on screen around, which
      // would look broken. Only the drawn text content changes.
      int lines = measureMessageLines(t.messages[i].body, bubbleWidth - 2 * bubblePadding);
      int h = lines * lineHeight + 2 * bubblePadding;

      if (y - h < msgRegionTop) break;  // no more room — rest is reachable only by scrolling

      y -= h;

      // Held-and-flipped bubble shows its full date + time instead of its
      // body.
      const char* displayText = t.messages[i].body;
      char full[32];
      if (i == convoFlippedIdx) {
        if (t.messages[i].hasRawTime) {
          char raw[24];
          reconstructRawTimestamp(t.messages[i], raw, sizeof(raw));
          formatFullTimestamp(raw, full, sizeof(full));
        } else {
          // No raw time available (message loaded from a prior session) —
          // fall back to whichever abbreviated form was already stored.
          copyBounded(full, t.messages[i].timestamp, sizeof(full));
        }
        displayText = full;
      }

      convoMsgBtns[i].initMsgButton(bubbleMarginX, y, bubbleWidth, h,
                                    displayText, t.messages[i].dir, t.messages[i].kept);
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

    // ── Reply bar — looks and sits like the compose screen's Msg field so
    // tapping it feels like continuing the conversation rather than jumping
    // to another screen. Set fields directly (skip Button::render()'s
    // rounded-rect/accent styling) so it can match that field's flat white
    // bar exactly; isClicked() still works off the stored x/y/w/h.
    convoReplyBtn.x      = 0;
    convoReplyBtn.y      = replyBarY;
    convoReplyBtn.width  = tft.width();
    convoReplyBtn.height = replyBarH;
    convoReplyBtn.text   = "";
    convoReplyBtn.color  = UI_FIELD;

    tft.fillRect(convoReplyBtn.x, convoReplyBtn.y, convoReplyBtn.width, convoReplyBtn.height, UI_FIELD);
    uiUseDefaultFont();
    tft.setTextColor(UI_TEXT_DIM);
    tft.setCursor(8, replyBarY + 8);
    tft.print("");
    tft.drawFastVLine(8, replyBarY + 8, 20, UI_ACCENT);
    tft.setFont(NULL);

    conversationDrawn = true;
  }

  // ── Bubble press/hold/release tracking — runs every frame (not just on
  // justPressed), since noticing a 2-second hold means watching the touch
  // persist across many loop() iterations, not just the instant it lands.
  // "kept" now toggles on RELEASE rather than touch-down: that's what lets
  // an ordinary tap be told apart from the start of a hold, so a hold
  // doesn't ALSO toggle kept along the way.
  if (touched) {
    int overIdx = -1;
    for (int i = convoVisibleTop; i <= convoVisibleBottom; i++) {
      if (convoMsgBtns[i].isClicked(sp)) { overIdx = i; break; }
    }

    if (justPressed) {
      convoPressIdx = overIdx;
      convoPressStartMillis = millis();
      convoLongPressFired = false;
    } else if (overIdx != convoPressIdx) {
      // Finger dragged off whatever bubble it started on — abandon
      // tracking so lifting elsewhere doesn't act on it.
      convoPressIdx = -1;
    } else if (convoPressIdx != -1 && !convoLongPressFired &&
               millis() - convoPressStartMillis >= CONVO_LONG_PRESS_MS) {
      // Held long enough on the same bubble — flip it (hold again to flip
      // back) and mark this press "consumed" so release below won't also
      // toggle kept.
      convoFlippedIdx = (convoFlippedIdx == convoPressIdx) ? -1 : convoPressIdx;
      convoLongPressFired = true;
      conversationDrawn = false;
    }
  } else if (convoPressIdx != -1) {
    // Finger just lifted. If this press never crossed the long-press
    // threshold, it was an ordinary tap.
    if (!convoLongPressFired) {
      int i = convoPressIdx;
      t.messages[i].kept = !t.messages[i].kept;

      if (t.messages[i].kept) {
        saveMessageToSD(t.phoneNumber, t.messages[i]);
      } else {
        unsaveMessageFromSD(t.phoneNumber, i);
        t.messages[i].saved = false;
      }

      conversationDrawn = false;
    }
    convoPressIdx = -1;
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

// FIX: this checked `readThread == true` to mean unread, backwards from the
// field's own meaning ("has this thread been read") and from how
// pushMessage() sets it (false on a new message). Combined with readThread
// never being set true anywhere (drawConversationToTFT didn't mark a thread
// read on open), this made unreadMessage() permanently return false.
bool unreadMessage() {
  for (int i = 0; i <= threadTop; i++) {
    if (!threads[i].readThread) {
      return true;
    }
  }
  return false; // every thread has been read (or there are no threads)
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

void pushMessage(const char* phone, const char* text, MsgDir dir, const char* time, const char* rawTime) {
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

  // ELSE

  MessageThread& t = threads[idx];  // reference t for shortcut

  // Since we have just received a message auto set message not being read yet 
  
  t.readThread = false;

  // For new / old thread/ If the conversation is not full, append new message
  if (t.lastMessageIndex + 1 < MAX_MESSAGES_PER_CONVO) {
    t.lastMessageIndex++;
    t.messages[t.lastMessageIndex].saved = false;

    // copy the individual message into the log of messages for this thread including its body, timestamp, and direction
    copyBounded(t.messages[t.lastMessageIndex].body, text, MAX_BODY_LEN);
    copyBounded(threads[idx].messages[t.lastMessageIndex].timestamp, time, MAX_TIMESTAMP_LEN);
    t.messages[t.lastMessageIndex].dir = dir;

    // Also set the message as being read to state false
    // This is neccesary because unread messages will be automatically saved to prevent persons from losing
    // messages they havent even seen yet.
    t.messages[t.lastMessageIndex].read = false;

    // Pack the raw date/time for the double-tap full-timestamp reveal (see
    // Msg::hasRawTime in messages.h) — same "yy/MM/dd,hh:mm:ss±zz" shape
    // UI.cpp's parseRawTimestamp() reads.
    Msg& newMsg = t.messages[t.lastMessageIndex];
    if (rawTime && strlen(rawTime) >= 14) {
        newMsg.hasRawTime = true;
        newMsg.rawYear   = (uint8_t)((rawTime[0]-'0')*10 + (rawTime[1]-'0'));
        newMsg.rawMonth  = (uint8_t)((rawTime[3]-'0')*10 + (rawTime[4]-'0'));
        newMsg.rawDay    = (uint8_t)((rawTime[6]-'0')*10 + (rawTime[7]-'0'));
        newMsg.rawHour   = (uint8_t)((rawTime[9]-'0')*10 + (rawTime[10]-'0'));
        newMsg.rawMinute = (uint8_t)((rawTime[12]-'0')*10 + (rawTime[13]-'0'));
    } else {
        newMsg.hasRawTime = false;
    }

    // copy message into a file of a specific phone number with all msgs in conversation

    // saveMessageToSD(phone, t.messages[t.lastMessageIndex]);

  } else {
    // TODO
  }

  // Most recently active thread moves to top
  moveThreadToTop(idx);

}

