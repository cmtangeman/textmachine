#include "settings.h"
#include "types.h"
#include "buttons.h"
#include "UI.h"
#include "modes.h"          // LED_SCREEN
#include "settings_icons.h" // CONTACTS_ONLY_ICON_bits

#include <Adafruit_GFX.h>
#include <Adafruit_ILI9341.h>

extern Adafruit_ILI9341 tft;
extern int brightness;  // 0 (brightest) .. 250 (darkest) — defined in textmachcap.ino, driven onto LED_SCREEN by modes.cpp's screenWake() and live here

// -------------------------------------------------------------------------------------------------
// Module state
// -------------------------------------------------------------------------------------------------

static bool settingsDrawn = false;

static Button settingsCloseBtn;    // top pill, chevron pointing down — closes back to UI_MENU
static Button contactsOnlyToggleBtn;
static Button brightnessSliderBtn; // whole tick row — tap anywhere on it, nearest tick wins

static bool contactsOnlyMode = false;  // visual only for now, per the request — no backend wiring yet

static const int BRIGHTNESS_LEVELS[] = {0, 62, 125, 187, 250};
static const int BRIGHTNESS_LEVEL_COUNT = 5;

// -------------------------------------------------------------------------------------------------
// Helpers
// -------------------------------------------------------------------------------------------------

static int nearestBrightnessLevel() {
    int best = 0;
    int bestDiff = abs(brightness - BRIGHTNESS_LEVELS[0]);
    for (int i = 1; i < BRIGHTNESS_LEVEL_COUNT; i++) {
        int diff = abs(brightness - BRIGHTNESS_LEVELS[i]);
        if (diff < bestDiff) { bestDiff = diff; best = i; }
    }
    return best;
}

// Small procedural sun glyph — no bitmap asset for this one since it's a
// handful of primitives (circle + rays), decorating the brightness slider's
// middle tick the same way UI_IMAGES/brightness_slider1.png does.
static void drawSunGlyph(int cx, int cy, uint16_t color) {
    tft.drawCircle(cx, cy, 4, color);
    const int rayInner = 7, rayOuter = 10;
    for (int a = 0; a < 360; a += 45) {
        float rad = a * DEG_TO_RAD;
        int x1 = cx + (int)(rayInner * cos(rad));
        int y1 = cy + (int)(rayInner * sin(rad));
        int x2 = cx + (int)(rayOuter * cos(rad));
        int y2 = cy + (int)(rayOuter * sin(rad));
        tft.drawLine(x1, y1, x2, y2, color);
    }
}

// -------------------------------------------------------------------------------------------------
// Public API
// -------------------------------------------------------------------------------------------------

void settingsScreenReset() {
    settingsDrawn = false;
}

bool settingsCloseBtnPressed(const ScreenPoint& sp) {
    return settingsCloseBtn.isClicked(sp);
}

void drawSettingsScreen(const ScreenPoint& sp, bool justPressed) {
    const int marginX = 14;

    if (!settingsDrawn) {
        tft.fillScreen(UI_BG);

        // ── Close pill — mirrors the home menu's swipe-up pill, flipped ──
        const int swipeW = 240 - 2 * marginX;
        const int swipeH = 26;
        settingsCloseBtn.x = marginX;
        settingsCloseBtn.y = 10;
        settingsCloseBtn.width  = swipeW;
        settingsCloseBtn.height = swipeH;
        drawSwipeIndicator(settingsCloseBtn.x, settingsCloseBtn.y, settingsCloseBtn.width, settingsCloseBtn.height, true);

        uiUseTitleFont();
        tft.setTextColor(UI_TEXT);
        tft.setCursor(marginX, 62);
        tft.print("Settings");
        tft.setFont(NULL);

        tft.drawFastHLine(0, 74, 240, UI_BORDER);

        // ── Contacts Only Mode row ──
        const int rowY = 90;
        tft.drawBitmap(marginX, rowY, CONTACTS_ONLY_ICON_bits, CONTACTS_ONLY_ICON_W, CONTACTS_ONLY_ICON_H, UI_TEXT);

        const int textX = marginX + CONTACTS_ONLY_ICON_W + 10;
        uiUseButtonFont();
        tft.setTextColor(UI_TEXT);
        tft.setCursor(textX, rowY + 14);
        tft.print("Contacts Only");

        uiUseDefaultFont();
        tft.setTextSize(1);
        tft.setTextColor(UI_TEXT_DIM);
        tft.setCursor(textX, rowY + 34);
        tft.print("Only receive messages");
        tft.setCursor(textX, rowY + 48);
        tft.print("from saved contacts");
        tft.setFont(NULL);

        // Two-segment OFF/ON toggle, centered under the icon+text block.
        const int toggleW = 120, toggleH = 32;
        const int toggleX = marginX;
        const int toggleY = rowY + CONTACTS_ONLY_ICON_H + 14;
        contactsOnlyToggleBtn.x = toggleX;
        contactsOnlyToggleBtn.y = toggleY;
        contactsOnlyToggleBtn.width  = toggleW;
        contactsOnlyToggleBtn.height = toggleH;

        tft.fillRoundRect(toggleX, toggleY, toggleW, toggleH, 8, UI_SURFACE);
        int halfW = toggleW / 2;
        uint16_t offColor = contactsOnlyMode ? UI_SURFACE : UI_ACCENT;
        uint16_t onColor  = contactsOnlyMode ? UI_ACCENT  : UI_SURFACE;
        tft.fillRoundRect(toggleX, toggleY, halfW, toggleH, 8, offColor);
        tft.fillRoundRect(toggleX + halfW, toggleY, toggleW - halfW, toggleH, 8, onColor);

        uiUseButtonFont();
        tft.setTextColor(UI_TEXT);
        tft.setCursor(toggleX + 16, toggleY + 21);
        tft.print("OFF");
        tft.setCursor(toggleX + halfW + 24, toggleY + 21);
        tft.print("ON");
        tft.setFont(NULL);

        // ── Brightness row ──
        const int brightRowY = toggleY + toggleH + 30;
        uiUseButtonFont();
        tft.setTextColor(UI_TEXT);
        tft.setCursor(marginX, brightRowY);
        tft.print("Brightness");
        tft.setFont(NULL);

        const int trackY = brightRowY + 40;
        const int trackX0 = marginX + 10;
        const int trackX1 = 240 - marginX - 10;
        const int tickSpan = trackX1 - trackX0;

        brightnessSliderBtn.x = marginX;
        brightnessSliderBtn.y = trackY - 20;
        brightnessSliderBtn.width  = 240 - 2 * marginX;
        brightnessSliderBtn.height = 40;

        tft.drawFastHLine(trackX0, trackY, tickSpan, UI_BORDER);

        int selectedLevel = nearestBrightnessLevel();
        for (int i = 0; i < BRIGHTNESS_LEVEL_COUNT; i++) {
            int tx = trackX0 + (tickSpan * i) / (BRIGHTNESS_LEVEL_COUNT - 1);
            if (i == BRIGHTNESS_LEVEL_COUNT / 2) {
                drawSunGlyph(tx, trackY, UI_TEXT_DIM);
            } else {
                tft.drawFastVLine(tx, trackY - 6, 12, UI_BORDER);
            }
            if (i == selectedLevel) {
                tft.fillCircle(tx, trackY, 6, UI_ACCENT);
            }
        }

        settingsDrawn = true;
    }

    if (!justPressed) return;

    if (contactsOnlyToggleBtn.isClicked(sp)) {
        contactsOnlyMode = !contactsOnlyMode;
        settingsDrawn = false;
        return;
    }

    if (brightnessSliderBtn.isClicked(sp)) {
        const int marginX2 = 14;
        const int trackX0 = marginX2 + 10;
        const int trackX1 = 240 - marginX2 - 10;
        const int tickSpan = trackX1 - trackX0;

        int nearest = 0;
        int bestDiff = 32000;
        for (int i = 0; i < BRIGHTNESS_LEVEL_COUNT; i++) {
            int tx = trackX0 + (tickSpan * i) / (BRIGHTNESS_LEVEL_COUNT - 1);
            int diff = abs((int)sp.x - tx);
            if (diff < bestDiff) { bestDiff = diff; nearest = i; }
        }

        brightness = BRIGHTNESS_LEVELS[nearest];
        analogWrite(LED_SCREEN, brightness);  // live preview, same pin/scale modes.cpp's screenWake() drives
        settingsDrawn = false;
        return;
    }
}
