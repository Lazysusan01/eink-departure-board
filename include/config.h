// ---------------------------------------------------------------------------
// Everything you need to personalise lives in this one file.
//
// This file is tracked by git, so it holds no real credentials - the WiFi
// password and the RTT token below are placeholders. Real values go in a
// gitignored `.env` at the project root, which scripts/load_env.py turns into
// -D defines before compilation. Every value here is #ifndef-guarded, so .env
// wins where it defines something and these defaults apply where it doesn't:
//
//     WIFI_SSID="your network"
//     WIFI_PASSWORD="your password"
//     RTT_REFRESH_TOKEN="ey..."
//
// See `.env.example`. An ESP32 has no filesystem to read secrets from at boot,
// so configuration is unavoidably compile-time; this split is what keeps that
// from meaning "in the repository".
//
// A partial .env is a valid state, not a build error - the rest simply falls
// through to the defaults below.
// ---------------------------------------------------------------------------
#pragma once

// --- WiFi ------------------------------------------------------------------
// 2.4 GHz only - the ESP32-WROOM-32D has no 5 GHz radio, so a router that
// merges both bands under one name may need the 2.4 GHz band split out.
#ifndef WIFI_SSID
#define WIFI_SSID       "your-network"
#endif
#ifndef WIFI_PASSWORD
#define WIFI_PASSWORD   "your-password"
#endif

// --- Where you are (for weather) -------------------------------------------
// Open-Meteo needs no API key. Decimal degrees; find yours on any map.
#ifndef WEATHER_LAT
#define WEATHER_LAT     51.4406           // West Dulwich
#endif
#ifndef WEATHER_LON
#define WEATHER_LON     -0.0879
#endif
#ifndef WEATHER_PLACE
#define WEATHER_PLACE   "West Dulwich"    // shown as the heading, purely cosmetic
#endif

// --- Trains ----------------------------------------------------------------
// RealTimeTrains v2: https://realtimetrains.github.io/api-specification/
// Registration issues a long-life *refresh* token (a JWT). The firmware swaps
// it for a short-life access token on every fetch - see trains.cpp.
//
// This replaced the old api.rtt.io v1 API, which used HTTP Basic auth with a
// username/password pair. If you have v1 credentials they will 401 here: the
// host, the auth scheme and the response shape all changed.
#ifndef RTT_REFRESH_TOKEN
  #ifdef RTT_PASSWORD
    // Back-compat: a .env written against the old naming still works.
    #define RTT_REFRESH_TOKEN RTT_PASSWORD
  #else
    #define RTT_REFRESH_TOKEN "FIXME_rtt_refresh_token"
  #endif
#endif
// Two stations, each its own block on the board. They are different walks from
// the flat, so they are shown separately rather than merged into one
// time-sorted list - "the next train" is not a meaningful idea when catching it
// means choosing which way to walk out of the door.
//
// The filter is a case-insensitive match against the destination name, with
// `|` separating alternatives. Empty means every train.
//
// There is no direction field in the API, so "northbound" has to be expressed
// as "these destinations". At West Dulwich that is trivial - everything towards
// town terminates at Victoria. At Tulse Hill it is a list, because Thameslink
// services run through to Bedford/St Pancras while Southern ones terminate at
// London Bridge; southbound there is Sutton, Selhurst, East Croydon and
// Norwood Junction. Both lists were taken from real six-hour samples.

#ifndef STATION_1_CRS
#define STATION_1_CRS     "WDU"
#endif
#ifndef STATION_1_NAME
#define STATION_1_NAME    "West Dulwich"
#endif
#ifndef STATION_1_FILTER
#define STATION_1_FILTER  "London Victoria"
#endif

#ifndef STATION_2_CRS
#define STATION_2_CRS     "TUH"
#endif
#ifndef STATION_2_NAME
#define STATION_2_NAME    "Tulse Hill"
#endif
#ifndef STATION_2_FILTER
#define STATION_2_FILTER  "Bedford|London Bridge|St Pancras|Blackfriars|Luton|Kentish Town"
#endif

// --- Refresh behaviour -----------------------------------------------------
// A full four-colour refresh on this panel measures ~22s on the bench and
// flashes through its colour planes the whole time, so the interval is a
// visual-comfort decision as much as a freshness one: at 10 minutes the board
// is strobing about 4% of the time, which is tolerable on a living-room wall.
#define REFRESH_MINUTES        10
#define RETRY_MINUTES          3          // used when a fetch failed

// Overnight the board stops refreshing. E-paper keeps the last image with zero
// power, so it still reads correctly - it just stops updating (and flashing).
#define QUIET_HOURS_ENABLED    true
#define QUIET_HOUR_START       23         // inclusive, local time
#define QUIET_HOUR_END         6          // exclusive

// --- Time ------------------------------------------------------------------
// POSIX TZ string. The default handles UK GMT/BST transitions automatically.
#define TIMEZONE       "GMT0BST,M3.5.0/1,M10.5.0/2"
#define NTP_SERVER_1   "pool.ntp.org"
#define NTP_SERVER_2   "time.nist.gov"

// --- Layout ----------------------------------------------------------------
// Portrait, for wall mounting. 1 = ribbon cable on the left, 3 = on the right.
// If the board comes up upside down, change this to the other value.
#define DISPLAY_ROTATION   3

// Per station, not in total. The trains panel is split between two stations, so
// each gets one emphasised row plus four more - roughly two hours ahead at
// these frequencies, which is the whole RTT_WINDOW_MINS the fetch asks for.
#define MAX_DEPARTURES     5
#define FORECAST_DAYS      4              // columns in the day forecast
