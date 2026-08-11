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
