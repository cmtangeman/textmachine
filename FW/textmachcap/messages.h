#ifndef MESSAGES_H
#define MESSAGES_H

#include <Arduino.h>
#include <Adafruit_ILI9341.h>
#include "types.h"

// -------------------------------------------------------------------------------------------------
// Constants
// -------------------------------------------------------------------------------------------------

#define MAX_CONVERSATIONS       10
#define MAX_MESSAGES_PER_CONVO  15
#define MAX_PHONE_LEN           20
#define MAX_BODY_LEN            60
#define MAX_TIMESTAMP_LEN 20
#define MAX_MSG_RENDER_LINES    6   // cap on wrapped lines a single msgButton bubble will print

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
  bool saved;  // already written to SD — distinct from `kept` below
  bool kept;   // user tapped this message to keep it (Snapchat-style keep indicator)
  bool read;

  // Packed raw date/time, RAM-only (never persisted to SD) — lets
  // drawConversationToTFT() show a genuinely complete timestamp (weekday +
  // date + time together) on double-tap, since `timestamp` above only ever
  // holds ONE of formatTimestamp()'s two abbreviated shapes. Stored as 5
  // packed bytes instead of the ~22-byte raw modem string verbatim: this
  // struct is instantiated MAX_CONVERSATIONS*MAX_MESSAGES_PER_CONVO (150)
  // times as a static array, and RAM here is extremely tight (see the
  // "Low memory" build warning) — the difference is ~750 bytes saved.
  // hasRawTime is false for messages loaded from a prior session (SD
  // doesn't store this), which falls back to showing just `timestamp`.
  bool hasRawTime;
  uint8_t rawYear;    // year - 2000
  uint8_t rawMonth;
  uint8_t rawDay;
  uint8_t rawHour;
  uint8_t rawMinute;
};

struct MessageThread {
  char phoneNumber[MAX_PHONE_LEN];
  Msg messages[MAX_MESSAGES_PER_CONVO]; // Instantiation of Msg/
  int lastMessageIndex;
  bool readThread;
};


// Helper
void normalizePhoneNumber(const char* input, char* output, int outLen);

// Word-wraps body against maxWidth (pixels, using the TFT's current font/size). If
// outLines is non-null, copies up to maxOutLines wrapped line strings into it (each
// bounded to MAX_BODY_LEN chars) — this is what msgButton::render() uses to actually
// print a bubble's text. Always returns the total line count, even if it exceeds
// maxOutLines.
int wrapMessageText(const char* body, int maxWidth, char outLines[][MAX_BODY_LEN], int maxOutLines);

// Returns the number of word-wrapped lines `body` takes up when rendered at maxWidth
// pixels (using the TFT's current font/size) — used to size a msgButton's height.
int measureMessageLines(const char* body, int maxWidth);

// notifications
// FIX: was declared `static` here, which gives every translation unit that
// includes this header its own private copy — textmachcap.ino calling it
// would fail to link against messages.cpp's definition. Needs external
// linkage since it's meant to be called from the menu screen.
bool unreadMessage();


// Compares two timestamps in EITHER of formatTimestamp()'s two output
// shapes (UI.cpp) — "Www HH:MM" (weekday + time, used within the last
// week) or "M/D/YYYY" (date only, used for anything older) — `a` and `b`
// don't need to be the same shape as each other. Returns >0 if `a` is more
// recent than `b`, <0 if `b` is more recent, 0 if they're equal or both
// unparseable (e.g. "Unknown", used when a send's timestamp couldn't be
// read from the modem).
//
// The weekday-only shape carries no date of its own, so it's anchored to
// `nowRaw` — a timestamp in the RAW modem format ("yy/MM/dd,hh:mm:ss±zz",
// e.g. straight from getCurrentRawTimestamp()), NOT formatTimestamp()'s
// output — a real reference date is needed to resolve "which Wednesday".
int compareTimestamps(const char* a, const char* b, const char* nowRaw);

// Storage
// `rawTime` is the raw modem timestamp ("yy/MM/dd,hh:mm:ss±zz") this
// specific message actually carries — an SMS's own CMGL timestamp for
// incoming, or getCurrentRawTimestamp() for outgoing — stored (packed) for
// the double-tap full-timestamp reveal. Pass nullptr if unavailable; the
// message just won't have that extra precision (matches how a message
// loaded from a prior session already has none).
void pushMessage(const char* phone, const char* text, MsgDir dir, const char* time, const char* rawTime);

void saveMessageToSD(const char* phone, Msg& msg);

void unsaveMessageFromSD(const char* phone, int msgIdx);

void loadMessagesFromSD();

// Recent messages UI
int  recentMessagesScreen(const ScreenPoint& sp, bool justPressed);
bool msgBackBtnPressed(const ScreenPoint& sp);
void recentMessagesReset();

// Conversation UI
// `touched` (the raw current touch state, distinct from `justPressed`) is
// needed to notice a bubble being held down across multiple frames — see
// the hold-to-flip tracking inside.
bool drawConversationToTFT(int selection, const ScreenPoint& sp, bool justPressed, bool touched);
bool convoBackBtnPressed(const ScreenPoint& sp);
bool convoReplyBtnPressed(const ScreenPoint& sp);
bool convoRxBtnPressed(const ScreenPoint& sp);
const char* getConversationPhone(int selection);

// Reverse of getConversationPhone(): the selection index (threadTop-relative,
// same numbering drawConversationToTFT()/getConversationPhone() use) a phone
// number currently sits at, or -1 if it has no open thread. Needed because
// receiving a message can reorder threads (moveThreadToTop) out from under a
// screen that's already open on one of them — see the in-conversation RX
// button handler in textmachcap.ino.
int getConversationSelectionByPhone(const char* phone);

// Number of messages currently stored for `phone`'s thread (0 if it has no
// thread). Used to tell whether a receive() just added a message to THIS
// conversation specifically, vs. some other thread — see the in-conversation
// RX button handler in textmachcap.ino.
int getConversationMessageCount(const char* phone);

void conversationReset();

#endif