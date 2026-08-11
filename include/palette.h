// ---------------------------------------------------------------------------
// The four inks this panel can actually produce.
//
// These are the RGB565 values GxEPD2 maps onto the panel's four planes. They
// are spelled out here rather than pulled from GxEPD2's headers so that the
// drawing code depends only on Adafruit_GFX and can be reasoned about (or
// rendered to a PC preview) without the display library present.
//
// Editorial rule for this board, worth keeping to:
//   BLACK   everything structural and every value you actually read
//   RED     exceptions only - delays, cancellations, severe weather
//   YELLOW  accents and the sun; never load-bearing on its own, because
//           yellow on white is the weakest contrast pair the panel has
// ---------------------------------------------------------------------------
#pragma once

#include <stdint.h>

#define C_BLACK   0x0000
#define C_WHITE   0xFFFF
#define C_RED     0xF800
#define C_YELLOW  0xFFE0
