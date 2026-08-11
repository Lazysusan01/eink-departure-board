#include "icons.h"

#include <math.h>

#include "palette.h"

namespace {

// The cloud is drawn twice - once inflated in black, once at true size in
// white - which yields an even outline of `grow` pixels without having to
// stroke each overlapping arc and then erase the seams between them.
void cloudShape(Adafruit_GFX& g, float cx, float cy, float w, float grow, uint16_t colour)
{
    const float rMain  = w * 0.27f + grow;
    const float rLeft  = w * 0.19f + grow;
    const float rRight = w * 0.22f + grow;

    g.fillCircle((int16_t)(cx),             (int16_t)(cy - w * 0.06f), (int16_t)rMain,  colour);
    g.fillCircle((int16_t)(cx - w * 0.27f), (int16_t)(cy + w * 0.08f), (int16_t)rLeft,  colour);
    g.fillCircle((int16_t)(cx + w * 0.27f), (int16_t)(cy + w * 0.06f), (int16_t)rRight, colour);

    const int16_t left   = (int16_t)(cx - w * 0.46f - grow);
    const int16_t right  = (int16_t)(cx + w * 0.49f + grow);
    const int16_t top    = (int16_t)(cy + w * 0.02f);
    const int16_t bottom = (int16_t)(cy + w * 0.28f + grow);
    g.fillRect(left, top, right - left, bottom - top, colour);
}

void drawCloud(Adafruit_GFX& g, float cx, float cy, float w)
{
    const float outline = fmaxf(2.0f, w * 0.035f);
    cloudShape(g, cx, cy, w, outline, C_BLACK);
    cloudShape(g, cx, cy, w, 0.0f, C_WHITE);
}

void drawSun(Adafruit_GFX& g, float cx, float cy, float r, bool withRays)
{
    const float outline = fmaxf(2.0f, r * 0.14f);

    if (withRays) {
        for (int i = 0; i < 8; i++) {
            const float angle = (float)i * (float)M_PI / 4.0f;
            const float x0 = cx + cosf(angle) * (r + outline + r * 0.35f);
            const float y0 = cy + sinf(angle) * (r + outline + r * 0.35f);
            const float x1 = cx + cosf(angle) * (r + outline + r * 0.85f);
            const float y1 = cy + sinf(angle) * (r + outline + r * 0.85f);
            drawThickLine(g, x0, y0, x1, y1, fmaxf(2.0f, r * 0.16f), C_BLACK);
        }
    }

    g.fillCircle((int16_t)cx, (int16_t)cy, (int16_t)(r + outline), C_BLACK);
    g.fillCircle((int16_t)cx, (int16_t)cy, (int16_t)r, C_YELLOW);
}

// A crescent, cut by overdrawing a second circle in the background colour.
void drawMoon(Adafruit_GFX& g, float cx, float cy, float r)
{
    const float outline = fmaxf(2.0f, r * 0.14f);
    g.fillCircle((int16_t)cx, (int16_t)cy, (int16_t)(r + outline), C_BLACK);
    g.fillCircle((int16_t)cx, (int16_t)cy, (int16_t)r, C_YELLOW);
    g.fillCircle((int16_t)(cx + r * 0.45f), (int16_t)(cy - r * 0.35f), (int16_t)(r * 0.92f), C_WHITE);
}

void drawRain(Adafruit_GFX& g, float cx, float cy, float w, int strokes, uint16_t colour)
{
    const float spacing = w * 0.24f;
    const float startX  = cx - spacing * (strokes - 1) / 2.0f;
    for (int i = 0; i < strokes; i++) {
        const float x = startX + spacing * i;
        drawThickLine(g, x + w * 0.05f, cy, x - w * 0.04f, cy + w * 0.22f,
                      fmaxf(2.0f, w * 0.045f), colour);
    }
}

void drawSnow(Adafruit_GFX& g, float cx, float cy, float w, int flakes)
{
    const float spacing = w * 0.26f;
    const float startX  = cx - spacing * (flakes - 1) / 2.0f;
    const float arm     = w * 0.08f;
    for (int i = 0; i < flakes; i++) {
        const float x = startX + spacing * i;
        const float y = cy + w * 0.11f;
        for (int a = 0; a < 3; a++) {
            const float angle = (float)a * (float)M_PI / 3.0f;
            drawThickLine(g, x - cosf(angle) * arm, y - sinf(angle) * arm,
                          x + cosf(angle) * arm, y + sinf(angle) * arm,
                          fmaxf(2.0f, w * 0.035f), C_BLACK);
        }
    }
}

void drawBolt(Adafruit_GFX& g, float cx, float cy, float w)
{
    const float s = w * 0.30f;
    // Two triangles form the zig-zag; drawn oversized in black first so the
    // yellow body reads against a white sky at small sizes.
    for (int pass = 0; pass < 2; pass++) {
        const float grow   = (pass == 0) ? fmaxf(2.0f, w * 0.03f) : 0.0f;
        const uint16_t col = (pass == 0) ? C_BLACK : C_YELLOW;
        g.fillTriangle((int16_t)(cx + s * 0.55f + grow), (int16_t)(cy - s - grow),
                       (int16_t)(cx - s * 0.55f - grow), (int16_t)(cy + s * 0.25f + grow),
                       (int16_t)(cx + s * 0.20f + grow), (int16_t)(cy + s * 0.25f + grow), col);
        g.fillTriangle((int16_t)(cx - s * 0.25f - grow), (int16_t)(cy + s * 1.05f + grow),
                       (int16_t)(cx + s * 0.60f + grow), (int16_t)(cy - s * 0.20f - grow),
                       (int16_t)(cx - s * 0.20f - grow), (int16_t)(cy - s * 0.20f - grow), col);
    }
}

void drawFog(Adafruit_GFX& g, float cx, float cy, float w)
{
    for (int i = 0; i < 4; i++) {
        const float y      = cy - w * 0.18f + i * w * 0.16f;
        const float inset  = (i % 2 == 0) ? w * 0.06f : w * 0.16f;
        drawThickLine(g, cx - w * 0.42f + inset, y, cx + w * 0.42f - inset, y,
                      fmaxf(2.0f, w * 0.055f), C_BLACK);
    }
}

}  // namespace

void drawThickLine(Adafruit_GFX& g, float x0, float y0, float x1, float y1,
                   float thickness, uint16_t colour)
{
    const float dx     = x1 - x0;
    const float dy     = y1 - y0;
    const float length = sqrtf(dx * dx + dy * dy);
    if (length < 0.001f) return;

    // Offset along the perpendicular so diagonals stay the requested width
    // rather than thinning out, which a naive x-only offset would do.
    const float px = -dy / length;
    const float py = dx / length;
    const int   steps = (int)fmaxf(1.0f, thickness);

    for (int i = 0; i < steps; i++) {
        const float offset = (float)i - (steps - 1) / 2.0f;
        g.drawLine((int16_t)lroundf(x0 + px * offset), (int16_t)lroundf(y0 + py * offset),
                   (int16_t)lroundf(x1 + px * offset), (int16_t)lroundf(y1 + py * offset), colour);
    }
}

void drawWeatherIcon(Adafruit_GFX& g, int16_t code, int16_t cx, int16_t cy,
                     int16_t size, bool isDay)
{
    const float w = (float)size;
    const float fx = (float)cx;
    const float fy = (float)cy;

    switch (code) {
        case 0:   // clear
        case 1:   // mainly clear
            if (isDay) drawSun(g, fx, fy, w * 0.30f, true);
            else       drawMoon(g, fx, fy, w * 0.32f);
            break;

        case 2:   // partly cloudy - luminary peeking out behind the cloud
            if (isDay) drawSun(g, fx + w * 0.20f, fy - w * 0.22f, w * 0.21f, true);
            else       drawMoon(g, fx + w * 0.20f, fy - w * 0.22f, w * 0.22f);
            drawCloud(g, fx - w * 0.05f, fy + w * 0.10f, w * 0.80f);
            break;

        case 3:   // overcast
            drawCloud(g, fx, fy, w * 0.90f);
            break;

        case 45:
        case 48:
            drawFog(g, fx, fy, w);
            break;

        case 51: case 53: case 55:
        case 56: case 57:
            drawCloud(g, fx, fy - w * 0.12f, w * 0.80f);
            drawRain(g, fx, fy + w * 0.22f, w, 2, C_BLACK);
            break;

        case 61: case 63: case 66: case 67:
        case 80: case 81:
            drawCloud(g, fx, fy - w * 0.12f, w * 0.80f);
            drawRain(g, fx, fy + w * 0.20f, w, 3, C_BLACK);
            break;

        case 65:  // heavy rain - the one weather worth colouring
        case 82:
            drawCloud(g, fx, fy - w * 0.12f, w * 0.80f);
            drawRain(g, fx, fy + w * 0.20f, w, 4, C_RED);
            break;

        case 71: case 73: case 75: case 77:
        case 85: case 86:
            drawCloud(g, fx, fy - w * 0.12f, w * 0.80f);
            drawSnow(g, fx, fy + w * 0.18f, w, 3);
            break;

        case 95: case 96: case 99:
            drawCloud(g, fx, fy - w * 0.16f, w * 0.80f);
            drawBolt(g, fx, fy + w * 0.22f, w);
            break;

        default:
            drawCloud(g, fx, fy, w * 0.85f);
            break;
    }
}

void drawSunGlyph(Adafruit_GFX& g, int16_t cx, int16_t cy, int16_t size, uint16_t colour)
{
    const float r = size * 0.28f;
    g.fillCircle(cx, cy, (int16_t)r, colour);
    for (int i = 0; i < 8; i++) {
        const float angle = (float)i * (float)M_PI / 4.0f;
        drawThickLine(g, cx + cosf(angle) * r * 1.45f, cy + sinf(angle) * r * 1.45f,
                      cx + cosf(angle) * r * 1.95f, cy + sinf(angle) * r * 1.95f,
                      fmaxf(2.0f, r * 0.30f), colour);
    }
}

void drawWindGlyph(Adafruit_GFX& g, int16_t cx, int16_t cy, int16_t size, uint16_t colour)
{
    const float w = (float)size;
    const float t = fmaxf(2.0f, w * 0.09f);
    drawThickLine(g, cx - w * 0.45f, cy - w * 0.20f, cx + w * 0.20f, cy - w * 0.20f, t, colour);
    drawThickLine(g, cx - w * 0.45f, cy + w * 0.06f, cx + w * 0.36f, cy + w * 0.06f, t, colour);
    drawThickLine(g, cx - w * 0.45f, cy + w * 0.32f, cx + w * 0.08f, cy + w * 0.32f, t, colour);
    // Curled tips, so it reads as moving air rather than a list of dashes.
    drawThickLine(g, cx + w * 0.20f, cy - w * 0.20f, cx + w * 0.30f, cy - w * 0.36f, t, colour);
    drawThickLine(g, cx + w * 0.36f, cy + w * 0.06f, cx + w * 0.44f, cy - w * 0.10f, t, colour);
}

void drawDropletGlyph(Adafruit_GFX& g, int16_t cx, int16_t cy, int16_t size, uint16_t colour)
{
    const float w = (float)size;
    g.fillCircle(cx, (int16_t)(cy + w * 0.14f), (int16_t)(w * 0.30f), colour);
    g.fillTriangle((int16_t)cx,               (int16_t)(cy - w * 0.46f),
                   (int16_t)(cx - w * 0.28f), (int16_t)(cy + w * 0.16f),
                   (int16_t)(cx + w * 0.28f), (int16_t)(cy + w * 0.16f), colour);
}

void drawTrainGlyph(Adafruit_GFX& g, int16_t cx, int16_t cy, int16_t size, uint16_t colour,
                    uint16_t bgColour)
{
    const float w = (float)size;
    const int16_t bodyW = (int16_t)(w * 0.66f);
    const int16_t bodyH = (int16_t)(w * 0.72f);
    const int16_t left  = (int16_t)(cx - bodyW / 2);
    const int16_t top   = (int16_t)(cy - bodyH / 2);

    g.fillRoundRect(left, top, bodyW, bodyH, (int16_t)(w * 0.16f), colour);
    // Window band, punched back out in whatever the glyph is sitting on.
    g.fillRect((int16_t)(left + w * 0.08f), (int16_t)(top + w * 0.11f),
               (int16_t)(bodyW - w * 0.16f), (int16_t)(w * 0.24f), bgColour);
    // Rails below the body.
    drawThickLine(g, cx - w * 0.44f, cy + w * 0.52f, cx + w * 0.44f, cy + w * 0.52f,
                  fmaxf(2.0f, w * 0.08f), colour);
}
