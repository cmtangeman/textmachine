#include "ui.h"
#include <Adafruit_GFX.h>
#include <Fonts/FreeSans9pt7b.h>
#include <Fonts/FreeSansBold9pt7b.h>
#include <Adafruit_ILI9341.h>

extern Adafruit_ILI9341 tft;

const uint16_t UI_BG     = ILI9341_BLACK;
const uint16_t UI_KEY    = ILI9341_DARKGREY;
const uint16_t UI_TEXT   = ILI9341_WHITE;
const uint16_t UI_ACCENT = ILI9341_BLUE;
const uint16_t UI_FIELD  = ILI9341_WHITE;

void uiUseDefaultFont() {
  tft.setFont(NULL);
  tft.setTextSize(2);
}

void uiUseButtonFont() {
  tft.setFont(&FreeSans9pt7b);
  tft.setTextSize(1);
}

void updateClock() {
    char raw[64];
    int i = 0;

    SerialSARA.println("AT+CCLK?");
    delay(300);
    while (SerialSARA.available() && i < 63) {
        raw[i++] = (char)SerialSARA.read();
    }
    raw[i] = '\0';

    if (strstr(raw, "+CCLK:") == NULL) return;  // no valid time

    // ── Parse raw "26/05/18,23:15:52-28" ─────────────────────
    // find opening quote
    char* ts = strchr(raw, '"');
    if (!ts) return;
    ts++;  // skip quote → now points to "26/05/18,23:15:52-28"

    // pull fields by fixed index position
    int month  = (ts[3] - '0') * 10 + (ts[4] - '0');  // 05
    int day    = (ts[6] - '0') * 10 + (ts[7] - '0');  // 18
    int hour   = (ts[9] - '0') * 10 + (ts[10] - '0'); // 23
    int minute = (ts[12] - '0') * 10 + (ts[13] - '0');// 15

    // parse timezone offset (quarter hours)
    int offsetQuarters = atoi(ts + 17);   // -28
    int offsetHours    = offsetQuarters / 4;  // -7

    // apply UTC offset
    hour = ((hour + offsetHours) % 24 + 24) % 24;  // handles negative wrap

    // convert to 12hr
    bool pm = hour >= 12;
    if (hour > 12) hour -= 12;
    if (hour == 0) hour = 12;

    // month name lookup
    const char* months[] = {
        "Jan","Feb","Mar","Apr","May","Jun",
        "Jul","Aug","Sep","Oct","Nov","Dec"
    };
    const char* monthName = (month >= 1 && month <= 12) ? months[month - 1] : "???";

    // ── Build display string ──────────────────────────────────
    char timeDisplay[20];
    snprintf(timeDisplay, sizeof(timeDisplay), "%s %d  %d:%02d %s",
        monthName, day, hour, minute, pm ? "PM" : "AM");
    //  "May 18  4:16 PM"

    // ── Draw on TFT ───────────────────────────────────────────
    tft.fillRect(0, 0, 200, 20, ILI9341_BLACK);  // clear clock area
    tft.setTextSize(1);
    tft.setTextColor(ILI9341_WHITE);
    tft.setCursor(0, 5);
    tft.print(timeDisplay);
}

void updateCSQ() {
    char raw[32];
    int i = 0;

    SerialSARA.println("AT+CSQ");
    delay(300);
    while (SerialSARA.available() && i < 31) {
        raw[i++] = (char)SerialSARA.read();
    }
    raw[i] = '\0';


    if (strstr(raw, "+CSQ:") == NULL) return;

    char* csqPtr = strstr(raw, "+CSQ: ");
    if (!csqPtr) return;
    int csq = atoi(csqPtr + 6);

    // map CSQ to bars
    int bars = 0;
    if      (csq == 99) bars = 0;
    else if (csq >= 20) bars = 4;
    else if (csq >= 15) bars = 3;
    else if (csq >= 10) bars = 2;
    else if (csq >= 5)  bars = 1;

  
    Serial.print("CSQ parsed: ");
    Serial.println(csq);
    Serial.print("Bars: ");
    Serial.println(bars);


    // ── Draw 4 bars top right ─────────────────────────────────
    int bx = 240;  // right edge x
    int by = 2;    // top y

    // bar widths and heights — left to right, shortest to tallest
    int barW = 4;
    int gap  = 2;
    int heights[4] = {4, 7, 10, 14};

    tft.fillRect(bx - 24, by, 26, 16, ILI9341_BLACK);  // clear area

    for (int b = 0; b < 4; b++) {
        int x = bx - (4 - b) * (barW + gap);
        int h = heights[b];
        int y = by + (14 - h);  // align bottoms
        uint16_t color = (b < bars) ? ILI9341_WHITE : 0x2104;  // filled or dim
        tft.fillRoundRect(x, y, barW, h, 1, color);
    }
}