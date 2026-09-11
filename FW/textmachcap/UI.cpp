#include "ui.h"
#include <Adafruit_GFX.h>
#include <Fonts/FreeSans9pt7b.h>
#include <Fonts/FreeSansBold9pt7b.h>
#include <Adafruit_ILI9341.h>

extern Adafruit_ILI9341 tft;
extern String readModemResponse(unsigned long timeout_ms);

// FIX: updateClock()/updateCSQ()/getCurrentTimestamp() each used to send
// their AT command then blindly delay(300) before reading whatever had
// arrived — a fixed wait on every call regardless of how fast the modem
// actually answered. readModemResponse() (already used everywhere else in
// this codebase) reads as bytes arrive and returns as soon as the response
// goes quiet for 150ms, so the common case is far under the cap below.
//
// FIX: a stray byte left in SerialSARA's RX buffer (a leftover unsolicited
// notification, or a fragment from a previous command that hit its own
// timeout mid-reply) used to satisfy readModemResponse()'s "we've started
// getting data" check on its own — its 150ms-of-silence-means-done logic
// could then return with just that stray fragment, before the modem's
// actual reply to *this* command ever arrived. Draining any leftover bytes
// before sending fixes the intermittent "response came back empty" failures
// this caused. Timeout bumped 800ms -> 1200ms to give the modem more
// headroom on a slow reply instead of racing it.
static void queryModem(const char* cmd, char* raw, size_t rawSize) {
    while (SerialSARA.available()) SerialSARA.read();  // drop anything left over from a prior exchange
    SerialSARA.println(cmd);
    String resp = readModemResponse(1200);
    size_t n = resp.length();
    if (n >= rawSize) n = rawSize - 1;
    memcpy(raw, resp.c_str(), n);
    raw[n] = '\0';
}

uint16_t UI_BG;
uint16_t UI_SURFACE;
uint16_t UI_BORDER;
uint16_t UI_TEXT;
uint16_t UI_TEXT_DIM;
uint16_t UI_ACCENT;
uint16_t UI_DANGER;
uint16_t UI_FIELD;

static bool uiColorsInit = false;

// tft.color565() needs the tft object constructed, so these can't be plain
// compile-time consts the way ILI9341_BLACK etc. are — same reason
// keyboard.cpp's own initColors() is lazy rather than a global initializer.
void uiInitColors() {
  if (uiColorsInit) return;
  UI_BG       = tft.color565(18,  18,  18);   // near-black, matches keyboard's COL_KB_BG
  UI_SURFACE  = tft.color565(32,  32,  34);   // one step up from UI_BG — rows/cards
  UI_BORDER   = tft.color565(46,  46,  48);   // hairline dividers
  UI_TEXT     = ILI9341_WHITE;
  UI_TEXT_DIM = tft.color565(142, 142, 147);  // iOS-style secondary label grey
  UI_ACCENT   = tft.color565(0,   122, 255);  // iOS blue, matches keyboard's COL_SEND
  UI_DANGER   = tft.color565(255, 69,  58);   // muted red — used sparingly
  UI_FIELD    = ILI9341_WHITE;
  uiColorsInit = true;
}

void uiUseDefaultFont() {
  tft.setFont(NULL);
  tft.setTextSize(2);
}

void uiUseButtonFont() {
  tft.setFont(&FreeSans9pt7b);
  tft.setTextSize(1);
}

void uiUseTitleFont() {
  tft.setFont(&FreeSansBold9pt7b);
  tft.setTextSize(1);
}

void updateClock() {
    char raw[64];
    queryModem("AT+CCLK?", raw, sizeof(raw));

    if (strstr(raw, "+CCLK:") == NULL) {
        // one retry — the first attempt is the one most likely to catch the
        // modem mid-reply to something else; give it a second shot before
        // giving up for this interval.
        queryModem("AT+CCLK?", raw, sizeof(raw));
        if (strstr(raw, "+CCLK:") == NULL) return;  // no valid time
    }

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

    // FIX: AT+CCLK? already returns *local* time on this module — the
    // trailing ±zz field just documents the offset the network (NITZ) used
    // to produce it, it isn't something the reader is meant to apply again.
    // This used to add offsetHours to `hour` a second time, silently
    // double-shifting the displayed clock by the timezone offset (e.g. 7
    // hours off in Pacific time). Parsed hour/minute are used as-is now.

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
    tft.fillRect(0, 0, 110, 20, UI_BG);  // clear clock area only — leaves battery/CSQ alone
    tft.setTextSize(1);
    tft.setTextColor(UI_TEXT_DIM);
    tft.setCursor(4, 5);
    tft.print(timeDisplay);
}

void updateCSQ() {
    char raw[32];
    queryModem("AT+CSQ", raw, sizeof(raw));


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

    tft.fillRect(bx - 24, by, 26, 16, UI_BG);  // clear area
    tft.setTextSize(1);
    tft.setTextColor(UI_TEXT_DIM);
    tft.setCursor(180, 5);
    tft.print(csq);
    tft.print("/30");
    for (int b = 0; b < 4; b++) {
        int x = bx - (4 - b) * (barW + gap);
        int h = heights[b];
        int y = by + (14 - h);  // align bottoms
        uint16_t color = (b < bars) ? UI_TEXT : UI_BORDER;  // filled or dim
        tft.fillRoundRect(x, y, barW, h, 1, color);
    }
}


void updateBattery() {
    int raw = analogRead(ADC_BATTERY);
    float vbat = (raw / 1023.0) * 3.3 * 1.275 ;  // MKR internal divider scales to 3.3V max
                                                // 1.275 is to account for internal voltage divider at EHB section of U1

    Serial.print("VBAT: ");
    Serial.print(vbat, 2);
    Serial.println("V");

    tft.fillRect(120, 0, 40, 20, UI_BG);  // clear battery area only
    tft.setTextSize(1);
    tft.setTextColor(UI_TEXT_DIM);
    tft.setCursor(120, 5);
    tft.print(vbat, 2);
    tft.print("V");
}

bool getCurrentTimestamp(char* output, int outLen) {
    char raw[64];
    queryModem("AT+CCLK?", raw, sizeof(raw));

    if (strstr(raw, "+CCLK:") == NULL) {
        queryModem("AT+CCLK?", raw, sizeof(raw));  // one retry, see updateClock()
        if (strstr(raw, "+CCLK:") == NULL) return false;  // no valid time
    }

    char* ts = strchr(raw, '"');
    if (!ts) return false;
    ts++;  // skip opening quote → "26/06/30,22:03:51-28"

    formatTimestamp(ts, output, outLen);
    return true;
}

void formatTimestamp(const char* raw, char* output, int outLen) {
    // raw = "26/06/30,22:03:51-28"
    //        0123456789012345678901

    if (!raw || strlen(raw) < 17) {
        strncpy(output, raw, outLen);
        return;
    }

    // parse fields by fixed index
    int year   = (raw[0]-'0')*10 + (raw[1]-'0');  // 26
    int month  = (raw[3]-'0')*10 + (raw[4]-'0');  // 06
    int day    = (raw[6]-'0')*10 + (raw[7]-'0');  // 30
    int hour   = (raw[9]-'0')*10 + (raw[10]-'0'); // 22
    int minute = (raw[12]-'0')*10 + (raw[13]-'0');// 03

    // FIX: this used to parse the trailing ±zz timezone field and add it to
    // `hour` again, then roll the calendar date across midnight to match —
    // but AT+CCLK? already returns local date/time (the ±zz field just
    // documents the offset the network applied to produce it), so this was
    // double-shifting every message timestamp by the timezone offset (e.g.
    // 7 hours off in Pacific time) and could even roll the date to the
    // wrong day. Parsed day/hour/minute are used as-is now — no rollover
    // needed since nothing is being shifted.
    int y = 2000 + year;

    // determine day of week using Zeller's formula
    // adjust month — Zeller treats Jan/Feb as months 13/14 of previous year
    int m = month;
    int zy = y;
    if (m < 3) { m += 12; zy--; }
    int k = zy % 100;
    int j = zy / 100;
    int dow = (day + (13*(m+1))/5 + k + k/4 + j/4 - 2*j) % 7;
    // Zeller: 0=Sat, 1=Sun, 2=Mon, 3=Tue, 4=Wed, 5=Thu, 6=Fri
    const char* days[] = {"Sat","Sun","Mon","Tue","Wed","Thu","Fri"};

    // get today for comparison — use AT+CCLK or hardcode check
    // for now just check if it was within last 7 days
    // simple approach: if day matches one of last 7 days show weekday
    // otherwise show date

    // determine if within this week — compare to current day
    // you already have updateClock() which reads AT+CCLK
    // for now just always show weekday + time
    snprintf(output, outLen, "%s %02d:%02d", days[dow], hour, minute);
}