// ---------------------------------------------------------------------------
// The data the board draws.
//
// These are plain-old-data structs with fixed char arrays rather than String,
// so the whole BoardData can live in RTC_DATA_ATTR memory and survive deep
// sleep. That is what lets a failed fetch still render yesterday's numbers
// marked stale, instead of a blank panel.
// ---------------------------------------------------------------------------
#pragma once

#include <stdint.h>
#include <stdbool.h>

#include "config.h"

struct WeatherNow {
    float   temperature;      // degC
    float   feelsLike;        // degC
    float   windKph;
    int16_t humidity;         // %
    int16_t code;             // WMO weather code
    bool    isDay;
    char    sunrise[6];       // "HH:MM"
    char    sunset[6];
};

struct WeatherDay {
    char    label[5];         // "Mon", or "Today"
    int16_t code;
    float   tempMin;
    float   tempMax;
    int16_t precipChance;     // %
};

struct Departure {
    char    destination[32];
    char    scheduled[6];     // "HH:MM"
    char    expected[12];     // "On time" | "HH:MM" | "Cancelled"
    char    platform[5];
    char    operatorName[26];
    bool    cancelled;
    bool    delayed;          // realtime differs from booked
    // Minutes from the last fetch until this train actually leaves, counted
    // from the realtime time when there is one. -1 when it could not be
    // worked out. This is the number you want on a wall: "9 min" answers
    // "do I need to leave now" without any subtraction on your part.
    int16_t minutesAway;
};

// One station's worth of board. Two of these are drawn, stacked, because the
// two stations are different walks - see config.h.
struct StationBoard {
    char      name[40];
    Departure departures[MAX_DEPARTURES];   // MAX_DEPARTURES is per station
    uint8_t   count;
    bool      ok;                            // this station's fetch succeeded
};

#define MAX_STATIONS 2

struct BoardData {
    WeatherNow   now;
    WeatherDay   days[FORECAST_DAYS];
    StationBoard stations[MAX_STATIONS];

    uint8_t     dayCount;
    uint8_t     stationCount;

    bool        weatherOk;
    bool        trainsOk;         // at least one station fetched

    char        lastUpdated[6];   // "HH:MM" of the last successful fetch
    bool        stale;            // rendered from RTC memory, not fresh

    // Captured once before drawing starts. GxEPD2 calls the render function
    // several times - once per horizontal band - so anything read from the
    // clock mid-render could tick over between bands and leave two pages
    // disagreeing about the date across a seam.
    char        headerDay[12];    // "Saturday"
    char        headerDate[24];   // "1 August 2026"
};
