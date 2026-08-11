// ---------------------------------------------------------------------------
// Bring-up diagnostics, built only into `pio run -e selftest`.
//
// The point is to separate the two things that can go wrong on a first flash.
// A blank panel could mean the driver substitution is wrong, the HSPI remap is
// wrong, or the FPC is not seated - none of which involve the network. So the
// self-test never touches WiFi: if these two screens draw, the display half of
// the project is proven and anything still broken is a fetch problem.
// ---------------------------------------------------------------------------
#pragma once

#include <Adafruit_GFX.h>

#include "model.h"

// Screen 1: colour bars, halftone swatches and corner markers. Answers "is the
// panel alive, are the four inks mapped to the constants I think they are, and
// is it the right way up".
void drawPanelTest(Adafruit_GFX& g);

// Screen 2: the real layout, filled with fixed plausible West Dulwich values.
// Proves renderBoard() end to end without a single HTTP call.
void buildSelfTestData(BoardData& data);
