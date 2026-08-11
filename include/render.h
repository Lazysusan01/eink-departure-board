#pragma once

#include <Adafruit_GFX.h>

#include "model.h"

// Draws the whole board. Called once per GxEPD2 page, so it must be a pure
// function of `data` - no network access, no clock reads that could change
// between bands, or the seams between pages would not line up.
void renderBoard(Adafruit_GFX& g, const BoardData& data);

// Full-screen message used before the first successful fetch and when WiFi
// cannot be reached at all.
void renderMessage(Adafruit_GFX& g, const char* title, const char* detail);

// The region a rush-hour partial refresh redraws: everything above the weather
// panel - top margin, masthead, both station blocks.
//
// It is one contiguous rectangle rather than two windows around the heroes,
// and that is the whole trick. Two partial refreshes would cost two refresh
// cycles, which on this panel is worse than one full one; and the masthead's
// "Updated hh:mm" is exactly the field that goes stale, so leaving it out
// would produce a board whose departures moved while its timestamp didn't.
// Everything that changes minute to minute is above NOW_Y; nothing below it
// is, which is what makes the split clean.
void rushRefreshRegion(int16_t& x, int16_t& y, int16_t& w, int16_t& h);
