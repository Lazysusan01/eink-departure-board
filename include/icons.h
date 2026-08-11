#pragma once

#include <Adafruit_GFX.h>
#include <stdint.h>

#include "palette.h"

// Weather icons are drawn with GFX primitives rather than stored as bitmaps:
// the board needs the same symbol at three very different sizes (hero, hourly
// strip, day forecast), and one vector routine covers all three without
// shipping three sets of image arrays in flash.
//
// `cx`/`cy` is the centre of the icon, `size` its nominal bounding width.
void drawWeatherIcon(Adafruit_GFX& g, int16_t code, int16_t cx, int16_t cy,
                     int16_t size, bool isDay);

// Small glyphs used by the panel headers and train rows.
// The sun glyph is flat and single-colour, unlike the layered sun inside
// drawWeatherIcon: it sits on the yellow masthead, where a yellow disc with a
// black outline would read as a hole rather than a sun.
void drawSunGlyph(Adafruit_GFX& g, int16_t cx, int16_t cy, int16_t size, uint16_t colour);
void drawWindGlyph(Adafruit_GFX& g, int16_t cx, int16_t cy, int16_t size, uint16_t colour);
void drawDropletGlyph(Adafruit_GFX& g, int16_t cx, int16_t cy, int16_t size, uint16_t colour);
// `bgColour` fills the window band punched out of the train body, so the glyph
// can sit on a reversed black bar as well as on white.
void drawTrainGlyph(Adafruit_GFX& g, int16_t cx, int16_t cy, int16_t size, uint16_t colour,
                    uint16_t bgColour = C_WHITE);
void drawThickLine(Adafruit_GFX& g, float x0, float y0, float x1, float y1,
                   float thickness, uint16_t colour);
