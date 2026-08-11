#include "selftest.h"

#include <Fonts/FreeSans9pt7b.h>
#include <Fonts/FreeSansBold9pt7b.h>
#include <Fonts/FreeSansBold12pt7b.h>
#include <Fonts/FreeSansBold18pt7b.h>
#include <string.h>

#include "icons.h"
#include "palette.h"

namespace {

constexpr int16_t W = 640;
constexpr int16_t H = 960;

void label(Adafruit_GFX& g, const GFXfont* font, uint16_t colour,
           int16_t x, int16_t y, const char* text)
{
    g.setFont(font);
    g.setTextSize(1);
    g.setTextColor(colour);
    g.setCursor(x, y);
    g.print(text);
}

// Same lattices as render.cpp, keyed to absolute screen coordinates. Reproduced
// here rather than shared because the point of the test is to check what the
// panel does with them, so it should not depend on the code under test.
void swatch(Adafruit_GFX& g, int16_t x, int16_t y, int16_t w, int16_t h,
            uint16_t colour, uint8_t density)
{
    for (int16_t py = y; py < y + h; py++) {
        for (int16_t px = x; px < x + w; px++) {
            bool hit;
            switch (density) {
                case 50: hit = ((px + py) & 1) == 0; break;
                case 25: hit = ((px & 1) == 0) && ((py & 1) == 0); break;
                default: hit = ((px & 3) == 0) && ((py & 1) == 0); break;
            }
            if (hit) g.drawPixel(px, py, colour);
        }
    }
}

}  // namespace

void drawPanelTest(Adafruit_GFX& g)
{
    g.fillScreen(C_WHITE);

    // A 2px frame inset by 4px. If any edge of this is missing, the drawable
    // area is not the 640x960 the layout assumes - which is the first thing to
    // rule out when a substituted driver class is involved.
    g.drawRect(4, 4, W - 8, H - 8, C_BLACK);
    g.drawRect(5, 5, W - 10, H - 10, C_BLACK);

    label(g, &FreeSansBold18pt7b, C_BLACK, 30, 70, "PANEL TEST");
    label(g, &FreeSans9pt7b, C_BLACK, 30, 96, "GDEM102F91 / 960x640 / SSD2677");

    // --- solid inks --------------------------------------------------------
    // Four bars, each labelled with the constant that produced it. On real
    // Spectra pigment red renders as a muted brick and yellow as mustard, so
    // the label is what confirms the mapping rather than the apparent hue.
    struct Bar { uint16_t colour; const char* name; uint16_t textColour; };
    const Bar bars[4] = {
        { C_WHITE,  "C_WHITE",  C_BLACK },
        { C_BLACK,  "C_BLACK",  C_WHITE },
        { C_RED,    "C_RED",    C_WHITE },
        { C_YELLOW, "C_YELLOW", C_BLACK },
    };
    const int16_t barTop = 120;
    const int16_t barH   = 150;
    const int16_t barW   = (W - 60) / 4;
    for (int i = 0; i < 4; i++) {
        const int16_t x = 30 + i * barW;
        g.fillRect(x, barTop, barW, barH, bars[i].colour);
        g.drawRect(x, barTop, barW, barH, C_BLACK);
        label(g, &FreeSansBold9pt7b, bars[i].textColour, x + 8, barTop + barH - 14,
              bars[i].name);
    }

    // --- halftones ---------------------------------------------------------
    // The board leans on these for apparent tone. If they moire, band, or read
    // as flat colour at viewing distance, the layout wants rethinking before
    // anything else is tuned.
    label(g, &FreeSansBold12pt7b, C_BLACK, 30, 310, "Halftones");
    const char* densityNames[3] = { "50%", "25%", "12%" };
    const uint8_t densities[3]  = { 50, 25, 12 };
    const int16_t swW = (W - 60) / 3;
    for (int i = 0; i < 3; i++) {
        const int16_t x = 30 + i * swW;
        g.drawRect(x, 325, swW, 80, C_BLACK);
        swatch(g, x + 1, 326, swW - 2, 78, C_YELLOW, densities[i]);
        g.drawRect(x, 410, swW, 80, C_BLACK);
        swatch(g, x + 1, 411, swW - 2, 78, C_BLACK, densities[i]);
        label(g, &FreeSans9pt7b, C_BLACK, x + 8, 508, densityNames[i]);
    }

    // --- glyphs ------------------------------------------------------------
    label(g, &FreeSansBold12pt7b, C_BLACK, 30, 560, "Glyphs");
    drawSunGlyph(g, 70, 610, 44, C_BLACK);
    drawWindGlyph(g, 160, 610, 44, C_BLACK);
    drawDropletGlyph(g, 250, 610, 44, C_BLACK);
    drawTrainGlyph(g, 340, 610, 44, C_BLACK, C_WHITE);
    for (int i = 0; i < 4; i++) {
        drawWeatherIcon(g, i == 0 ? 0 : i == 1 ? 3 : i == 2 ? 63 : 95,
                        440 + (i % 2) * 90, 590 + (i / 2) * 60, 46, true);
    }

    // --- orientation -------------------------------------------------------
    // Unambiguous corner markers. "TL" appearing bottom-right means the
    // rotation constant is 180 degrees out; mirrored text means the panel is
    // being addressed in the wrong scan direction.
    label(g, &FreeSansBold12pt7b, C_BLACK, 16, 30, "TL");
    label(g, &FreeSansBold12pt7b, C_BLACK, W - 56, 30, "TR");
    label(g, &FreeSansBold12pt7b, C_BLACK, 16, H - 16, "BL");
    label(g, &FreeSansBold12pt7b, C_BLACK, W - 56, H - 16, "BR");

    // A wedge that is only symmetric one way up, so a 180-degree flip is
    // obvious even without reading the corner text.
    g.fillTriangle(W / 2 - 40, H - 60, W / 2 + 40, H - 60, W / 2, H - 130, C_RED);

    label(g, &FreeSans9pt7b, C_BLACK, 30, 700,
          "If all four bars, both halftone rows and TL/TR/BL/BR are");
    label(g, &FreeSans9pt7b, C_BLACK, 30, 722,
          "present and upright, the display half is working.");
    label(g, &FreeSans9pt7b, C_BLACK, 30, 744,
          "The red wedge points UP at the bottom of the board.");
}

void buildSelfTestData(BoardData& data)
{
    memset(&data, 0, sizeof(data));

    snprintf(data.headerDay,   sizeof(data.headerDay),   "%s", "Saturday");
    snprintf(data.headerDate,  sizeof(data.headerDate),  "%s", "10 August 2026");
    snprintf(data.lastUpdated, sizeof(data.lastUpdated), "%s", "18:42");
    data.weatherOk = true;
    data.trainsOk  = true;
    data.stale     = false;

    data.now.temperature = 21.4f;
    data.now.feelsLike   = 20.1f;
    data.now.windKph     = 13.0f;
    data.now.humidity    = 64;
    data.now.code        = 3;      // overcast
    data.now.isDay       = true;
    snprintf(data.now.sunrise, sizeof(data.now.sunrise), "%s", "05:47");
    snprintf(data.now.sunset,  sizeof(data.now.sunset),  "%s", "20:24");

    // Deliberately varied codes: the live corpus tends to give four identical
    // overcast icons, which hides whether the icon routine is switching at all.
    const int16_t dayCodes[4] = { 3, 61, 0, 80 };
    const float   dayMax[4]   = { 22.0f, 19.5f, 24.1f, 20.8f };
    const float   dayMin[4]   = { 14.2f, 13.0f, 15.6f, 13.9f };
    const int16_t dayPop[4]   = { 15, 80, 5, 55 };
    const char*   dayLabel[4] = { "Today", "Sun", "Mon", "Tue" };
    data.dayCount = FORECAST_DAYS < 4 ? FORECAST_DAYS : 4;
    for (uint8_t i = 0; i < data.dayCount; i++) {
        snprintf(data.days[i].label, sizeof(data.days[i].label), "%s", dayLabel[i]);
        data.days[i].code         = dayCodes[i];
        data.days[i].tempMax      = dayMax[i];
        data.days[i].tempMin      = dayMin[i];
        data.days[i].precipChance = dayPop[i];
    }

    // On-time, delayed and cancelled all appear across the two stations: every
    // state the row renderer has a branch for, so a single screen exercises all
    // of them. Platform is deliberately blank, matching what RTT v2 supplies.
    struct Row {
        const char* dest; const char* sched; const char* exp;
        const char* op; bool cancelled; bool delayed; int16_t away;
    };
    const char* names[MAX_STATIONS] = { STATION_1_NAME, STATION_2_NAME };
    const Row rows[MAX_STATIONS][5] = {
        {
            { "London Victoria", "18:51", "On time",   "Southeastern", false, false,  9 },
            { "London Victoria", "19:06", "19:11",     "Southeastern", false, true,  24 },
            { "London Victoria", "19:21", "Cancelled", "Southeastern", true,  false, -1 },
            { "London Victoria", "19:36", "On time",   "Southeastern", false, false, 54 },
            { "London Victoria", "19:51", "On time",   "Southeastern", false, false, 69 },
        },
        {
            { "London Bridge",   "18:55", "On time",   "Southern",     false, false, 13 },
            { "Bedford",         "19:08", "On time",   "Thameslink",   false, false, 26 },
            { "London Bridge",   "19:25", "19:29",     "Southern",     false, true,  43 },
            { "St Pancras Intl", "19:38", "On time",   "Thameslink",   false, false, 56 },
            { "Bedford",         "19:53", "Cancelled", "Thameslink",   true,  false, -1 },
        },
    };

    data.stationCount = MAX_STATIONS;
    for (uint8_t s = 0; s < MAX_STATIONS; s++) {
        StationBoard& board = data.stations[s];
        snprintf(board.name, sizeof(board.name), "%s", names[s]);
        board.ok    = true;
        board.count = MAX_DEPARTURES < 5 ? MAX_DEPARTURES : 5;
        for (uint8_t i = 0; i < board.count; i++) {
            Departure& row = board.departures[i];
            snprintf(row.destination,  sizeof(row.destination),  "%s", rows[s][i].dest);
            snprintf(row.scheduled,    sizeof(row.scheduled),    "%s", rows[s][i].sched);
            snprintf(row.expected,     sizeof(row.expected),     "%s", rows[s][i].exp);
            snprintf(row.operatorName, sizeof(row.operatorName), "%s", rows[s][i].op);
            row.platform[0] = '\0';
            row.cancelled   = rows[s][i].cancelled;
            row.delayed     = rows[s][i].delayed;
            row.minutesAway = rows[s][i].away;
        }
    }
}
