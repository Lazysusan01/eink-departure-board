// ---------------------------------------------------------------------------
// Wiring for the Good Display ESP32-L development kit + DESPI-C02 adapter.
//
// Taken verbatim from GxEPD2's own examples/GxEPD2_HelloWorld/
// GxEPD2_wiring_examples.h:24 - "mapping of Good Display ESP32 Development Kit
// ESP32-L, e.g. to DESPI-C02" - not from the Waveshare ESP32 driver board,
// which is a different board with a different mapping and was what this file
// used to claim. If in doubt, that file in the installed library is the
// authority, and it is the only one that was ever right.
//
// SCK/SDI are the ESP32's default VSPI pins, so unlike the Waveshare board
// this needs no SPIClass re-mapping at all - the default SPI object is
// already on the right pins.
//
// !! RES is on GPIO12, which is also the MTDI strapping pin that selects
// flash voltage: high at reset means 1.8V, and this board's 3.3V flash then
// cannot be read at all (esptool reports "Failed to communicate with the
// flash chip", flash ID ff/ffff). RES is active-low and the panel's ribbon
// holds it high, so with the panel connected the ESP32 cannot boot until the
// XPD_SDIO efuse is burned to force 3.3V. See README.
// ---------------------------------------------------------------------------
#pragma once

#define BOARD_GOOD_DISPLAY_ESP32_L 1
//#define BOARD_WAVESHARE_ESP32    1   // different mapping, see below
//#define BOARD_GENERIC_ESP32      1   // hand-wired, conventional VSPI pins

#if defined(BOARD_GENERIC_ESP32)
  #define EPD_BUSY   4
  #define EPD_RST   16
  #define EPD_DC    17
  #define EPD_CS     5
  #define EPD_SCK   18
  #define EPD_MISO  19
  #define EPD_MOSI  23
  #define EPD_USE_HSPI 0
#elif defined(BOARD_WAVESHARE_ESP32)
  // Waveshare ESP32 driver board: unusual SPI pins, needs the HSPI remap.
  #define EPD_BUSY  25
  #define EPD_RST   26
  #define EPD_DC    27
  #define EPD_CS    15
  #define EPD_SCK   13
  #define EPD_MISO  35   // input-only; never GPIO12, which is a strapping pin
  #define EPD_MOSI  14
  #define EPD_USE_HSPI 1
#else   // Good Display ESP32-L + DESPI-C02
  #define EPD_BUSY  13
  #define EPD_RST   12   // == MTDI strapping pin, see warning above
  #define EPD_DC    14
  #define EPD_CS    27
  #define EPD_SCK   18   // VSPI default
  #define EPD_MISO  19   // VSPI default; unused, the panel is write-only
  #define EPD_MOSI  23   // VSPI default
  #define EPD_USE_HSPI 0
#endif
