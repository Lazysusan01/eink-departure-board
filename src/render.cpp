#include "render.h"

#include <Fonts/FreeSans9pt7b.h>
#include <Fonts/FreeSans12pt7b.h>
#include <Fonts/FreeSansBold9pt7b.h>
#include <Fonts/FreeSansBold12pt7b.h>
#include <Fonts/FreeSansBold18pt7b.h>
#include <Fonts/FreeSansBold24pt7b.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "icons.h"
#include "palette.h"
#include "weather.h"

// ---------------------------------------------------------------------------
// Layout. Portrait 640x960, wall mounted, read from a few metres away.
//
// The visual language is a transit poster: heavy black bars, one yellow field
// per screen, and halftones. Halftones are the trick that makes four inks feel
// like more than four - a checkerboard of yellow and white reads as a pale
// gold, black on white as a grey - without inventing a colour the panel
// cannot actually print.
// ---------------------------------------------------------------------------
namespace {

constexpr int16_t BOARD_W = 640;
constexpr int16_t BOARD_H = 960;
constexpr int16_t MARGIN  = 26;
constexpr int16_t CONTENT_W = BOARD_W - 2 * MARGIN;

// Departures sit directly under the masthead, above the weather. The train is
// the only thing on this board anyone acts on - the weather you glance at, the
// 22:09 you either catch or miss - so it gets the top half, where the eye
// lands first and where you can read it without walking closer.
//
// The masthead is the one panel nobody reads twice - you know what day it is -
// so it pays the smallest rent per pixel.
//
// There used to be an hourly temperature strip between "now" and the forecast.
// It went to pay for the second station: with two blocks to fit, 336px gave
// each of them one emphasised departure and two rows, which is under an hour
// of look-ahead at these frequencies and less than the fetch already asks the
// API for. The strip was also the weakest panel on the board - it answered
// "will it be colder at 10pm", a question nobody standing in a hallway with a
// coat in their hand is asking. Its 170px doubles the departures instead.
constexpr int16_t HEADER_Y = 0;
constexpr int16_t HEADER_H = 72;
constexpr int16_t TRAINS_Y = HEADER_Y + HEADER_H;      // 72
constexpr int16_t TRAINS_H = 506;
constexpr int16_t NOW_Y    = TRAINS_Y + TRAINS_H;      // 578
constexpr int16_t NOW_H    = 200;
constexpr int16_t DAILY_Y  = NOW_Y + NOW_H;            // 778
constexpr int16_t DAILY_H  = 150;
constexpr int16_t FOOTER_Y = DAILY_Y + DAILY_H;        // 928

constexpr int16_t BAR_H = 26;   // reversed section bar

enum Align { ALIGN_LEFT, ALIGN_CENTRE, ALIGN_RIGHT };

// ---------------------------------------------------------------------------
// Halftones
// ---------------------------------------------------------------------------
// Densities are lattices in *absolute* screen coordinates, never coordinates
// relative to the region being filled. Two adjacent patches therefore share
// one continuous grid, and - which matters more - the pattern does not shift
// between GxEPD2's horizontal bands and leave a visible seam mid-region.
enum Density : uint8_t { DENSITY_50 = 50, DENSITY_25 = 25, DENSITY_12 = 12 };

inline bool patternHit(int16_t x, int16_t y, uint8_t density)
{
    switch (density) {
        case DENSITY_50: return ((x + y) & 1) == 0;
        case DENSITY_25: return ((x & 1) == 0) && ((y & 1) == 0);
        default:         return ((x & 3) == 0) && ((y & 1) == 0);
    }
}

void fillPattern(Adafruit_GFX& g, int16_t x, int16_t y, int16_t w, int16_t h,
                 uint16_t colour, uint8_t density)
{
    if (w <= 0 || h <= 0) return;
    for (int16_t py = y; py < y + h; py++) {
        for (int16_t px = x; px < x + w; px++) {
            if (patternHit(px, py, density)) g.drawPixel(px, py, colour);
        }
    }
}

// ---------------------------------------------------------------------------
// Text
// ---------------------------------------------------------------------------
int16_t textWidth(Adafruit_GFX& g, const char* text)
{
    int16_t x1, y1;
    uint16_t w, h;
    g.getTextBounds(text, 0, 0, &x1, &y1, &w, &h);
    return (int16_t)w;
}

// `y` is the text baseline, which is how GFX custom fonts position glyphs.
int16_t drawText(Adafruit_GFX& g, const GFXfont* font, uint8_t scale, uint16_t colour,
                 int16_t x, int16_t y, Align align, const char* text)
{
    g.setFont(font);
    g.setTextSize(scale);
    g.setTextColor(colour);   // single-arg: transparent background, so white
                              // text drops cleanly onto a black bar

    const int16_t w = textWidth(g, text);
    int16_t drawX = x;
    if (align == ALIGN_CENTRE) drawX = x - w / 2;
    else if (align == ALIGN_RIGHT) drawX = x - w;

    g.setCursor(drawX, y);
    g.print(text);
    return w;
}

// The bundled FreeSans fonts only cover ASCII 0x20-0x7E, so there is no degree
// glyph to print - it gets drawn as a ring scaled to the text it follows.
void drawDegreeRing(Adafruit_GFX& g, int16_t x, int16_t capTop, int16_t radius,
                    uint16_t colour, uint16_t holeColour)
{
    const int16_t cy = capTop + radius + radius / 3;
    g.fillCircle(x + radius, cy, radius, colour);
    g.fillCircle(x + radius, cy, radius > 3 ? radius - 2 : radius - 1, holeColour);
}

// Draws e.g. "17" followed by a proportionally sized degree ring, and returns
// the total advance so callers can lay out anything that follows.
int16_t drawTemperature(Adafruit_GFX& g, const GFXfont* font, uint8_t scale, uint16_t colour,
                        int16_t x, int16_t y, Align align, float value,
                        uint16_t holeColour = C_WHITE)
{
    char buffer[8];
    snprintf(buffer, sizeof(buffer), "%d", (int)lroundf(value));

    g.setFont(font);
    g.setTextSize(scale);

    int16_t bx, by;
    uint16_t bw, bh;
    g.getTextBounds(buffer, 0, 0, &bx, &by, &bw, &bh);

    const int16_t ringRadius = (int16_t)fmaxf(3.0f, bh * 0.16f);
    const int16_t gap        = (int16_t)fmaxf(2.0f, bh * 0.06f);
    const int16_t total      = (int16_t)bw + gap + ringRadius * 2;

    int16_t startX = x;
    if (align == ALIGN_CENTRE) startX = x - total / 2;
    else if (align == ALIGN_RIGHT) startX = x - total;

    drawText(g, font, scale, colour, startX, y, ALIGN_LEFT, buffer);
    drawDegreeRing(g, startX + (int16_t)bw + gap, y + by, ringRadius, colour, holeColour);
    return total;
}

// Truncates with a trailing ".." rather than an ellipsis glyph, which the
// bundled fonts do not carry either. Always probes against the original
// `text`, never against `out`, so source and destination cannot overlap.
void fitText(Adafruit_GFX& g, const GFXfont* font, const char* text,
             int16_t maxWidth, char* out, size_t outSize)
{
    g.setFont(font);
    g.setTextSize(1);
    out[0] = '\0';
    if (maxWidth <= 0) return;

    snprintf(out, outSize, "%s", text);
    if (textWidth(g, out) <= maxWidth) return;

    for (size_t length = strlen(text); length > 1; length--) {
        char probe[64];
        snprintf(probe, sizeof(probe), "%.*s..", (int)(length - 1), text);
        if (textWidth(g, probe) <= maxWidth) {
            snprintf(out, outSize, "%s", probe);
            return;
        }
    }
    snprintf(out, outSize, "..");
}

// A solid black bar with the label reversed out of it. Heavier than a rule and
// a hairline, which is what lets the five panels separate at four metres.
// `leftInset` reserves room at the start of the bar for a glyph. The right-hand
// label is right-aligned, so anything placed near that end collides with it.
void drawSectionBar(Adafruit_GFX& g, int16_t y, const char* left, const char* right,
                    int16_t leftInset = 0)
{
    g.fillRect(MARGIN, y, CONTENT_W, BAR_H, C_BLACK);
    drawText(g, &FreeSansBold9pt7b, 1, C_WHITE, MARGIN + 14 + leftInset, y + 18,
             ALIGN_LEFT, left);
    if (right) {
        drawText(g, &FreeSansBold9pt7b, 1, C_WHITE, BOARD_W - MARGIN - 14, y + 18,
                 ALIGN_RIGHT, right);
    }
}

// ---------------------------------------------------------------------------
// Panels
// ---------------------------------------------------------------------------
void drawHeader(Adafruit_GFX& g, const BoardData& data)
{
    // Full-bleed yellow. Yellow is the panel's weakest ink against white, but
    // as a *field* under black type it is the strongest thing here - which is
    // the only way this panel can use it at scale.
    g.fillRect(0, HEADER_Y, BOARD_W, HEADER_H, C_YELLOW);
    g.fillRect(0, HEADER_Y + HEADER_H - 5, BOARD_W, 5, C_BLACK);

    // Day and date share one baseline rather than stacking. That is what buys
    // the 28px back without dropping the day to a smaller face - the masthead
    // still has to carry from across the room.
    const int16_t dayW = drawText(g, &FreeSansBold24pt7b, 1, C_BLACK, MARGIN,
                                  HEADER_Y + 48, ALIGN_LEFT, data.headerDay);
    drawText(g, &FreeSans12pt7b, 1, C_BLACK, MARGIN + dayW + 16, HEADER_Y + 48,
             ALIGN_LEFT, data.headerDate);

    drawText(g, &FreeSansBold12pt7b, 1, C_BLACK, BOARD_W - MARGIN, HEADER_Y + 32,
             ALIGN_RIGHT, WEATHER_PLACE);

    if (data.weatherOk && data.now.sunrise[0] && data.now.sunset[0]) {
        char sun[32];
        snprintf(sun, sizeof(sun), "%s - %s", data.now.sunrise, data.now.sunset);
        drawText(g, &FreeSans9pt7b, 1, C_BLACK, BOARD_W - MARGIN, HEADER_Y + 58,
                 ALIGN_RIGHT, sun);
        drawSunGlyph(g, BOARD_W - MARGIN - textWidth(g, sun) - 18, HEADER_Y + 52, 20, C_BLACK);
    }
}

void drawNow(Adafruit_GFX& g, const BoardData& data)
{
    if (!data.weatherOk) {
        drawText(g, &FreeSans12pt7b, 1, C_RED, BOARD_W / 2, NOW_Y + NOW_H / 2, ALIGN_CENTRE,
                 "Weather unavailable");
        return;
    }

    const WeatherNow& now = data.now;

    // A quarter-tone panel behind the icon, so the largest empty area on the
    // board carries some texture instead of reading as a printing error.
    fillPattern(g, BOARD_W - MARGIN - 190, NOW_Y + 12, 190, NOW_H - 36, C_YELLOW, DENSITY_25);

    drawTemperature(g, &FreeSansBold24pt7b, 2, C_BLACK, MARGIN, NOW_Y + 88, ALIGN_LEFT,
                    now.temperature);

    drawText(g, &FreeSansBold18pt7b, 1, C_BLACK, MARGIN, NOW_Y + 134, ALIGN_LEFT,
             weatherDescription(now.code));

    // One stats line: feels-like, wind, humidity, separated by black pips.
    const int16_t statsY = NOW_Y + 176;
    int16_t cursor = MARGIN;

    char feels[24];
    snprintf(feels, sizeof(feels), "Feels %d", (int)lroundf(now.feelsLike));
    cursor += drawText(g, &FreeSans12pt7b, 1, C_BLACK, cursor, statsY, ALIGN_LEFT, feels);
    drawDegreeRing(g, cursor + 3, statsY - 17, 4, C_BLACK, C_WHITE);
    cursor += 16;

    g.fillCircle(cursor + 8, statsY - 6, 3, C_BLACK);
    cursor += 22;

    drawWindGlyph(g, cursor + 12, statsY - 6, 24, C_BLACK);
    cursor += 30;
    char wind[24];
    snprintf(wind, sizeof(wind), "%d km/h", (int)lroundf(now.windKph));
    cursor += drawText(g, &FreeSans12pt7b, 1, C_BLACK, cursor, statsY, ALIGN_LEFT, wind);
    cursor += 8;

    g.fillCircle(cursor + 8, statsY - 6, 3, C_BLACK);
    cursor += 22;

    drawDropletGlyph(g, cursor + 8, statsY - 7, 20, C_BLACK);
    cursor += 20;
    char humidity[16];
    snprintf(humidity, sizeof(humidity), "%d%%", now.humidity);
    drawText(g, &FreeSans12pt7b, 1, C_BLACK, cursor, statsY, ALIGN_LEFT, humidity);

    drawWeatherIcon(g, now.code, BOARD_W - MARGIN - 95, NOW_Y + 96, 158, now.isDay);
}

void drawDaily(Adafruit_GFX& g, const BoardData& data)
{
    drawSectionBar(g, DAILY_Y + 6, "FORECAST", "HIGH / LOW");

    if (!data.weatherOk || data.dayCount == 0) return;

    // The range bars share one scale across all days, so a short bar means a
    // genuinely narrow day rather than an artefact of per-column scaling.
    float lowest = data.days[0].tempMin;
    float highest = data.days[0].tempMax;
    for (uint8_t i = 1; i < data.dayCount; i++) {
        lowest  = fminf(lowest, data.days[i].tempMin);
        highest = fmaxf(highest, data.days[i].tempMax);
    }
    const float span = fmaxf(1.0f, highest - lowest);

    const int16_t columnW = CONTENT_W / data.dayCount;
    for (uint8_t i = 0; i < data.dayCount; i++) {
        const WeatherDay& day = data.days[i];
        const int16_t x0 = MARGIN + columnW * i;
        const int16_t cx = x0 + columnW / 2;

        // Today gets a halftone column rather than a heavier typeface: it
        // marks the column without making the other three look inactive.
        if (i == 0) {
            fillPattern(g, x0 + 2, DAILY_Y + BAR_H + 10, columnW - 4, DAILY_H - BAR_H - 22,
                        C_YELLOW, DENSITY_25);
        }
        if (i > 0) g.drawFastVLine(x0, DAILY_Y + 44, DAILY_H - 58, C_BLACK);

        drawText(g, &FreeSansBold12pt7b, 1, C_BLACK, cx, DAILY_Y + 62, ALIGN_CENTRE, day.label);
        drawWeatherIcon(g, day.code, cx, DAILY_Y + 92, 42, true);
        drawTemperature(g, &FreeSansBold12pt7b, 1, C_BLACK, cx - 24, DAILY_Y + 128,
                        ALIGN_CENTRE, day.tempMax);
        drawTemperature(g, &FreeSans12pt7b, 1, C_BLACK, cx + 30, DAILY_Y + 128,
                        ALIGN_CENTRE, day.tempMin);

        const int16_t barX = x0 + 18;
        const int16_t barW = columnW - 36;
        const int16_t barY = DAILY_Y + 140;
        g.drawFastHLine(barX, barY, barW, C_BLACK);

        const int16_t fillStart = barX + (int16_t)((day.tempMin - lowest) / span * barW);
        const int16_t fillEnd   = barX + (int16_t)((day.tempMax - lowest) / span * barW);
        g.fillRect(fillStart, barY - 3, (int16_t)fmaxf(3.0f, (float)(fillEnd - fillStart)), 7,
                   day.precipChance >= 50 ? C_RED : C_BLACK);
    }
}

// Platform as a reversed badge: a number in a black tile reads as a platform
// at a glance, where "Plat 2" set in small type does not.
void drawPlatformBadge(Adafruit_GFX& g, int16_t cx, int16_t cy, const char* platform,
                       bool large)
{
    if (!platform[0] || strcmp(platform, "-") == 0) return;
    const int16_t size = large ? 32 : 24;
    g.fillRect(cx - size / 2, cy - size / 2, size, size, C_BLACK);
    drawText(g, large ? &FreeSansBold12pt7b : &FreeSansBold9pt7b, 1, C_WHITE,
             cx, cy + (large ? 6 : 5), ALIGN_CENTRE, platform);
}

void drawHeroDeparture(Adafruit_GFX& g, const Departure& train, int16_t y, int16_t h)
{
    g.fillRect(MARGIN, y, CONTENT_W, h, C_YELLOW);
    g.fillRect(MARGIN, y, CONTENT_W, 3, C_BLACK);
    g.fillRect(MARGIN, y + h - 3, CONTENT_W, 3, C_BLACK);
    g.fillRect(MARGIN, y, 3, h, C_BLACK);
    g.fillRect(BOARD_W - MARGIN - 3, y, 3, h, C_BLACK);

    const int16_t baseline = y + h / 2 + 9;

    drawText(g, &FreeSansBold18pt7b, 1, C_BLACK, MARGIN + 18, baseline, ALIGN_LEFT,
             train.scheduled);

    // The countdown is the whole reason this row is emphasised - it answers
    // "do I need to leave now" without any subtraction on the reader's part.
    int16_t countdownW = 0;
    if (train.cancelled) {
        countdownW = drawText(g, &FreeSansBold18pt7b, 1, C_RED, BOARD_W - MARGIN - 18,
                              baseline, ALIGN_RIGHT, "Cancelled");
    } else if (train.minutesAway >= 0 && train.minutesAway <= 180) {
        char countdown[16];
        if (train.minutesAway == 0) snprintf(countdown, sizeof(countdown), "due");
        else snprintf(countdown, sizeof(countdown), "%d min", train.minutesAway);
        countdownW = drawText(g, &FreeSansBold18pt7b, 1,
                              train.delayed ? C_RED : C_BLACK,
                              BOARD_W - MARGIN - 18, baseline, ALIGN_RIGHT, countdown);
    } else {
        countdownW = drawText(g, &FreeSansBold12pt7b, 1, train.delayed ? C_RED : C_BLACK,
                              BOARD_W - MARGIN - 18, baseline, ALIGN_RIGHT, train.expected);
    }

    const int16_t badgeX = BOARD_W - MARGIN - 18 - countdownW - 34;
    drawPlatformBadge(g, badgeX, y + h / 2, train.platform, true);

    const int16_t destX = MARGIN + 132;
    char destination[48];
    fitText(g, &FreeSansBold12pt7b, train.destination, badgeX - 24 - destX,
            destination, sizeof(destination));
    drawText(g, &FreeSansBold12pt7b, 1, C_BLACK, destX, baseline - 2, ALIGN_LEFT, destination);

    if (train.cancelled) {
        g.setFont(&FreeSansBold12pt7b);
        g.setTextSize(1);
        drawThickLine(g, destX, baseline - 9, destX + textWidth(g, destination), baseline - 9,
                      3.0f, C_RED);
    }
}

void drawDepartureRow(Adafruit_GFX& g, const Departure& train, int16_t y, int16_t rowH,
                      bool shaded)
{
    // Alternating halftone bands, which is what keeps the eye on one line
    // across 588px without needing a rule between every row.
    if (shaded) fillPattern(g, MARGIN, y, CONTENT_W, rowH, C_BLACK, DENSITY_12);

    const int16_t baseline = y + rowH / 2 + 7;

    drawText(g, &FreeSansBold12pt7b, 1, C_BLACK, MARGIN + 18, baseline, ALIGN_LEFT,
             train.scheduled);

    const uint16_t statusColour = (train.cancelled || train.delayed) ? C_RED : C_BLACK;
    const int16_t statusW = drawText(g, &FreeSansBold9pt7b, 1, statusColour,
                                     BOARD_W - MARGIN - 18, baseline, ALIGN_RIGHT,
                                     train.expected);

    drawPlatformBadge(g, BOARD_W - MARGIN - 18 - statusW - 34, y + rowH / 2,
                      train.platform, false);

    const int16_t destX  = MARGIN + 132;
    const int16_t destW  = BOARD_W - MARGIN - 18 - statusW - 62 - destX;
    char destination[48];
    fitText(g, &FreeSans12pt7b, train.destination, destW, destination, sizeof(destination));
    drawText(g, &FreeSans12pt7b, 1, C_BLACK, destX, baseline, ALIGN_LEFT, destination);

    if (train.cancelled) {
        g.setFont(&FreeSans12pt7b);
        g.setTextSize(1);
        drawThickLine(g, destX, baseline - 7, destX + textWidth(g, destination), baseline - 7,
                      2.0f, C_RED);
    }
}

// One station: its own bar, its own emphasised next train, its own rows. Each
// block is self-contained because the two stations are separate decisions -
// which is also why an unavailable one says so in place rather than blanking
// the whole panel.
void drawStationBlock(Adafruit_GFX& g, const StationBoard& board, int16_t y, int16_t h)
{
    char station[48];
    fitText(g, &FreeSansBold9pt7b, board.name[0] ? board.name : "Station",
            CONTENT_W - 250, station, sizeof(station));

    drawSectionBar(g, y, station, "NORTHBOUND", 26);
    drawTrainGlyph(g, MARGIN + 24, y + 13, 22, C_WHITE, C_BLACK);

    const int16_t listY = y + 30;
    const int16_t listH = h - 34;

    if (!board.ok) {
        drawText(g, &FreeSans12pt7b, 1, C_RED, BOARD_W / 2, listY + listH / 2 + 6,
                 ALIGN_CENTRE, "Departures unavailable");
        return;
    }
    if (board.count == 0) {
        drawText(g, &FreeSans12pt7b, 1, C_BLACK, BOARD_W / 2, listY + listH / 2 + 6,
                 ALIGN_CENTRE, "Nothing northbound");
        return;
    }

    // The hero keeps a fixed height and the rest of the block is divided
    // between however many rows follow, rather than the other way round: the
    // next train is the one thing here worth a fixed, predictable position, and
    // late at night when only two services are left the rows simply get taller.
    const int16_t heroH     = 60;
    const int16_t remaining = board.count - 1;

    drawHeroDeparture(g, board.departures[0], listY, heroH);

    if (remaining > 0) {
        const int16_t rowH = (listH - heroH - 4) / remaining;
        for (int16_t i = 0; i < remaining; i++) {
            drawDepartureRow(g, board.departures[i + 1], listY + heroH + 4 + rowH * i,
                             rowH, (i % 2) == 0);
        }
    }
}

void drawTrains(Adafruit_GFX& g, const BoardData& data)
{
    // The panel's height is split evenly rather than by how many departures
    // each station has: a block that moves as services thin out overnight is
    // harder to read at a glance than one that always sits in the same place.
    // 506 - 18 of gaps, halved: 244 each, which is a bar, a hero and four rows.
    constexpr int16_t GAP    = 6;
    const int16_t     blockH = (TRAINS_H - GAP * (MAX_STATIONS + 1)) / MAX_STATIONS;

    for (uint8_t i = 0; i < MAX_STATIONS && i < data.stationCount; i++) {
        drawStationBlock(g, data.stations[i], TRAINS_Y + GAP + i * (blockH + GAP), blockH);
    }
}

void drawFooter(Adafruit_GFX& g, const BoardData& data)
{
    g.fillRect(MARGIN, FOOTER_Y + 2, CONTENT_W, 2, C_BLACK);

    char updated[48];
    snprintf(updated, sizeof(updated), "Updated %s",
             data.lastUpdated[0] ? data.lastUpdated : "--:--");
    const int16_t w = drawText(g, &FreeSans9pt7b, 1, C_BLACK, MARGIN, FOOTER_Y + 24,
                               ALIGN_LEFT, updated);

    if (data.stale) {
        drawText(g, &FreeSansBold9pt7b, 1, C_RED, MARGIN + w + 12, FOOTER_Y + 24, ALIGN_LEFT,
                 "- NOT REFRESHED");
    }

    drawText(g, &FreeSans9pt7b, 1, C_BLACK, BOARD_W - MARGIN, FOOTER_Y + 24, ALIGN_RIGHT,
             "Open-Meteo / RealTimeTrains");
}

}  // namespace

void renderBoard(Adafruit_GFX& g, const BoardData& data)
{
    g.fillScreen(C_WHITE);
    drawHeader(g, data);
    drawTrains(g, data);
    drawNow(g, data);
    drawDaily(g, data);
    drawFooter(g, data);
}

void renderMessage(Adafruit_GFX& g, const char* title, const char* detail)
{
    g.fillScreen(C_WHITE);
    g.fillRect(0, BOARD_H / 2 - 150, BOARD_W, 100, C_YELLOW);
    g.fillRect(0, BOARD_H / 2 - 55, BOARD_W, 5, C_BLACK);
    drawText(g, &FreeSansBold24pt7b, 1, C_BLACK, BOARD_W / 2, BOARD_H / 2 - 88, ALIGN_CENTRE,
             title);
    drawText(g, &FreeSans12pt7b, 1, C_BLACK, BOARD_W / 2, BOARD_H / 2 + 10, ALIGN_CENTRE,
             detail);
}
