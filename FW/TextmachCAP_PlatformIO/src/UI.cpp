#include "UI.h"
#include <Adafruit_GFX.h>
#include <Fonts/FreeSans9pt7b.h>
#include <Fonts/FreeSansBold9pt7b.h>
#include <Adafruit_ILI9341.h>

extern Adafruit_ILI9341 tft;
extern String readModemResponse(unsigned long timeout_ms);

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
  UI_ACCENT   = tft.color565(75, 83, 32); // deep military green 
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

bool getCurrentRawTimestamp(char* output, int outLen) {
    char raw[64];
    queryModem("AT+CCLK?", raw, sizeof(raw));

    if (strstr(raw, "+CCLK:") == NULL) {
        queryModem("AT+CCLK?", raw, sizeof(raw));  // one retry, see updateClock()
        if (strstr(raw, "+CCLK:") == NULL) return false;  // no valid time
    }

    char* ts = strchr(raw, '"');
    if (!ts) return false;
    ts++;  // skip opening quote → "26/06/30,22:03:51-28"

    copyBounded(output, ts, outLen);
    return true;
}

bool getCurrentTimestamp(char* output, int outLen) {
    char raw[32];
    if (!getCurrentRawTimestamp(raw, sizeof(raw))) return false;
    formatTimestamp(raw, raw, output, outLen);  // formatting "now" against itself — always 0 days ago
    return true;
}

// Parses the "yy/MM/dd,hh:mm:ss±zz" shape shared by AT+CCLK? and a CMGL
// line's quoted timestamp field. Returns false (out params untouched) if
// `raw` is too short to hold the fields we read.
static bool parseRawTimestamp(const char* raw, int* year, int* month, int* day, int* hour, int* minute) {
    if (!raw || strlen(raw) < 17) return false;
    *year   = 2000 + (raw[0]-'0')*10 + (raw[1]-'0');  // 26 -> 2026
    *month  = (raw[3]-'0')*10 + (raw[4]-'0');         // 06
    *day    = (raw[6]-'0')*10 + (raw[7]-'0');         // 30
    *hour   = (raw[9]-'0')*10 + (raw[10]-'0');        // 22
    *minute = (raw[12]-'0')*10 + (raw[13]-'0');       // 03
    return true;
}

// Days since 1970-01-01 for the proleptic Gregorian calendar (Howard
// Hinnant's days_from_civil) — correct across month/year boundaries and
// leap years, which a cyclic weekday-only comparison can't do. Only used
// here to diff two nearby dates, not as a real calendar library.
static long daysFromCivil(int y, int m, int d) {
    y -= m <= 2;
    long era = (y >= 0 ? y : y - 399) / 400;
    unsigned yoe = (unsigned)(y - era * 400);                          // [0, 399]
    unsigned doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;      // [0, 365]
    unsigned doe = yoe * 365 + yoe/4 - yoe/100 + doy;                  // [0, 146096]
    return era * 146097 + (long)doe - 719468;
}

void formatTimestamp(const char* raw, const char* nowRaw, char* output, int outLen) {
    // raw = "26/06/30,22:03:51-28"
    //        0123456789012345678901

    int year, month, day, hour, minute;
    if (!parseRawTimestamp(raw, &year, &month, &day, &hour, &minute)) {
        strncpy(output, raw ? raw : "", outLen);
        output[outLen - 1] = '\0';
        return;
    }

    // FIX: this used to parse the trailing ±zz timezone field and add it to
    // `hour` again, then roll the calendar date across midnight to match —
    // but AT+CCLK? already returns local date/time (the ±zz field just
    // documents the offset the network applied to produce it), so this was
    // double-shifting every message timestamp by the timezone offset (e.g.
    // 7 hours off in Pacific time) and could even roll the date to the
    // wrong day. Parsed day/hour/minute are used as-is now — no rollover
    // needed since nothing is being shifted.

    // How many days ago was this, relative to `nowRaw`? Needs the actual
    // calendar date (not just a weekday name) to get right across
    // month/year boundaries — a message from "last Tuesday" three weeks ago
    // and one from yesterday can share a weekday name otherwise.
    int daysAgo = 0;
    int nYear, nMonth, nDay, nHour, nMinute;
    if (parseRawTimestamp(nowRaw, &nYear, &nMonth, &nDay, &nHour, &nMinute)) {
        long diff = daysFromCivil(nYear, nMonth, nDay) - daysFromCivil(year, month, day);
        if (diff > 0) daysAgo = (int)diff;  // negative (clock skew / formatting "now" itself) treated as 0
    }
    // if nowRaw doesn't parse, daysAgo stays 0 — falls back to the recent-weekday branch below

    if (daysAgo >= 7) {
        // Older than a week — a weekday name alone would be ambiguous (which
        // Wednesday?), so show the date instead. No leading zeros, matches
        // typical US short-date style: "9/5/2026", not "09/05/2026".
        snprintf(output, outLen, "%d/%d/%d", month, day, year);
        return;
    }

    // determine day of week using Zeller's formula
    // adjust month — Zeller treats Jan/Feb as months 13/14 of previous year
    int m = month;
    int zy = year;
    if (m < 3) { m += 12; zy--; }
    int k = zy % 100;
    int j = zy / 100;
    int dow = (day + (13*(m+1))/5 + k + k/4 + j/4 - 2*j) % 7;
    // Zeller: 0=Sat, 1=Sun, 2=Mon, 3=Tue, 4=Wed, 5=Thu, 6=Fri
    const char* days[] = {"Sat","Sun","Mon","Tue","Wed","Thu","Fri"};

    snprintf(output, outLen, "%s %02d:%02d", days[dow], hour, minute);
}

void formatMessageTime(const char* raw, char* output, int outLen) {
    int year, month, day, hour, minute;
    if (!parseRawTimestamp(raw, &year, &month, &day, &hour, &minute)) {
        strncpy(output, raw ? raw : "", outLen);
        output[outLen - 1] = '\0';
        return;
    }

    bool pm = hour >= 12;
    int h12 = hour % 12;
    if (h12 == 0) h12 = 12;
    snprintf(output, outLen, "%d:%02d %s", h12, minute, pm ? "PM" : "AM");
}

void formatFullTimestamp(const char* raw, char* output, int outLen) {
    int year, month, day, hour, minute;
    if (!parseRawTimestamp(raw, &year, &month, &day, &hour, &minute)) {
        strncpy(output, raw ? raw : "", outLen);
        output[outLen - 1] = '\0';
        return;
    }

    char timePart[16];
    formatMessageTime(raw, timePart, sizeof(timePart));

    int m = month;
    int zy = year;
    if (m < 3) { m += 12; zy--; }
    int k = zy % 100;
    int j = zy / 100;
    int dow = (day + (13*(m+1))/5 + k + k/4 + j/4 - 2*j) % 7;  // 0=Sat..6=Fri
    const char* days[] = {"Sat","Sun","Mon","Tue","Wed","Thu","Fri"};

    snprintf(output, outLen, "%s %d/%d/%d %s", days[dow], month, day, year, timePart);
}




  bool  islandIsClicked(const ScreenPoint& sp) {
    return (sp.x >= 0 && sp.x <= 240 && sp.y >= 0 && sp.y <= 26);
}

void drawSwipeIndicator(int x, int y, int w, int h, bool pointDown) {
    tft.fillRoundRect(x, y, w, h, h / 2, UI_SURFACE);

    int cx = x + w / 2;
    int cy = y + h / 2;
    int armW = 8;   // chevron half-width
    int armH = 5;   // chevron half-height
    int top    = pointDown ? cy - armH : cy + armH;
    int bottom = pointDown ? cy + armH : cy - armH;

    // Two stacked passes, 1px apart, for a slightly bolder chevron —
    // Adafruit_GFX has no built-in thick-line draw.
    for (int dy = 0; dy <= 1; dy++) {
        tft.drawLine(cx - armW, top + dy, cx, bottom + dy, UI_TEXT_DIM);
        tft.drawLine(cx, bottom + dy, cx + armW, top + dy, UI_TEXT_DIM);
    }
}