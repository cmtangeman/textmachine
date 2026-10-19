#ifndef TYPES_H
#define TYPES_H

#include <Arduino.h>

#define MAX_CONTACTS  50
#define MAX_TIMESTAMP_LEN 20
#define MAX_NAME_LEN  20
#define MAX_PHONE_LEN 20
#define MAX_BODY_RECEIVE_LEN 200

// ── Display ───────────────────────────────────────────────────────

static void copyBounded(char* dst, const char* src, size_t dstSize) {
  if (dstSize == 0) return;
  strncpy(dst, src, dstSize - 1);
  dst[dstSize - 1] = '\0';
}

struct ScreenPoint {
  int16_t x;
  int16_t y;
  ScreenPoint(int16_t x = 0, int16_t y = 0) : x(x), y(y) {}
};

struct Contact {
  char name[MAX_NAME_LEN];
  char phone[MAX_PHONE_LEN];
};

// Enums 

enum keyboardMode {KB_COMPOSE, KB_ADD_CONTACT, KB_DEBUG};


#endif // TYPES_H