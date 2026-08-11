#include "trains.h"

#include <Arduino.h>
#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <ctype.h>
#include <string.h>

// ---------------------------------------------------------------------------
// RealTimeTrains v2 (https://realtimetrains.github.io/api-specification/).
//
// This is a different API from the old api.rtt.io v1: different host, Bearer
// tokens instead of HTTP Basic, ISO-8601 timestamps instead of bare "HHMM",
// and a much more nested service object. Two calls are needed per fetch:
//
//   1. GET /api/get_access_token   with the long-life *refresh* token
//      -> a short-life access token (about an hour)
//   2. GET /gb-nr/location?code=…  with that access token
//
// The token is deliberately re-fetched every wake rather than cached in RTC
// memory. It would survive deep sleep, but a cached credential that expires
// mid-flight fails in a way that looks exactly like a network fault, and the
// board wakes only every REFRESH_MINUTES - so the saving is one TLS handshake
// per ten minutes in exchange for a whole class of confusing bug.
//
// Two stations are fetched per wake, sharing one access token.
// ---------------------------------------------------------------------------
namespace {

constexpr char RTT_HOST[] = "https://data.rtt.io";

// Two hours. The default window is 60 minutes, which at these frequencies
// yields fewer services than the panel has room for; three hours yields a
// response big enough to truncate when the heap is busy after the weather
// fetch. Two comfortably fills MAX_DEPARTURES.
constexpr int RTT_WINDOW_MINS = 120;

// "2026-08-10T20:39:00" -> "20:39". Returns false if it isn't a timestamp.
bool isoClock(const char* iso, char* out, size_t outSize)
{
    out[0] = '\0';
    if (!iso || strlen(iso) < 16 || iso[10] != 'T') return false;
    snprintf(out, outSize, "%.5s", iso + 11);
    return true;
}

// Minutes from now until an ISO-8601 local timestamp. Times in the recent past
// read as already gone rather than as ~22 hours away, which is what a naive
// wrap would make of a board fetched just after midnight.
int16_t minutesUntilIso(const char* iso)
{
    if (!iso || strlen(iso) < 16 || iso[10] != 'T') return -1;

    const time_t rawNow = time(nullptr);
    if (rawNow < 1700000000) return -1;   // clock not set; no answer beats a wrong one

    struct tm local;
    localtime_r(&rawNow, &local);

    const int nowMinutes = local.tm_hour * 60 + local.tm_min;
    const int depMinutes = (iso[11] - '0') * 600 + (iso[12] - '0') * 60 +
                           (iso[14] - '0') * 10  + (iso[15] - '0');

    int delta = depMinutes - nowMinutes;
    if (delta < -120) delta += 24 * 60;   // rolled past midnight
    return (delta < 0 || delta > 24 * 60) ? -1 : (int16_t)delta;
}

bool containsIgnoreCase(const char* haystack, const char* needle)
{
    if (!needle || needle[0] == '\0') return true;
    if (!haystack) return false;

    for (const char* start = haystack; *start; start++) {
        const char* h = start;
        const char* n = needle;
        while (*h && *n && tolower((unsigned char)*h) == tolower((unsigned char)*n)) {
            h++;
            n++;
        }
        if (*n == '\0') return true;
    }
    return false;
}

// Case-insensitive match against a `|`-separated list of alternatives. An empty
// list matches everything, so an unfiltered station needs no special case.
// There is no direction field in the API, so this list is how "northbound" is
// expressed - see config.h.
bool matchesAnyOf(const char* haystack, const char* alternatives)
{
    if (!alternatives || alternatives[0] == '\0') return true;
    if (!haystack) return false;

    char one[64];
    const char* segment = alternatives;
    while (*segment) {
        const char* bar = strchr(segment, '|');
        const size_t len = bar ? (size_t)(bar - segment) : strlen(segment);
        if (len > 0 && len < sizeof(one)) {
            memcpy(one, segment, len);
            one[len] = '\0';
            if (containsIgnoreCase(haystack, one)) return true;
        }
        if (!bar) break;
        segment = bar + 1;
    }
    return false;
}

// Step 1: swap the long-life refresh token for a short-life access token.
bool fetchAccessToken(char* out, size_t outSize)
{
    WiFiClientSecure client;
    client.setInsecure();   // no root store on the device; see weather.cpp

    HTTPClient http;
    http.setTimeout(12000);
    if (!http.begin(client, String(RTT_HOST) + "/api/get_access_token")) {
        Serial.println(F("[trains] token begin() failed"));
        return false;
    }
    http.addHeader("Authorization", String("Bearer ") + RTT_REFRESH_TOKEN);
    http.addHeader("Accept", "application/json");

    const int status = http.GET();
    if (status != HTTP_CODE_OK) {
        Serial.printf("[trains] token HTTP %d\n", status);
        http.end();
        return false;
    }

    const String body = http.getString();
    http.end();

    JsonDocument doc;
    const DeserializationError err = deserializeJson(doc, body);
    if (err) {
        Serial.printf("[trains] token parse failed: %s (%d bytes)\n",
                      err.c_str(), body.length());
        return false;
    }

    const char* token = doc["token"] | "";
    if (token[0] == '\0') {
        Serial.println(F("[trains] token missing from response"));
        return false;
    }
    snprintf(out, outSize, "%s", token);
    return true;
}

// Only these fields are kept while parsing. The unfiltered response carries a
// full schedule per service; the board draws six values.
void buildFilter(JsonDocument& filter)
{
    filter["query"]["location"]["description"] = true;

    JsonObject service = filter["services"].add<JsonObject>();

    JsonObject temporal  = service["temporalData"].to<JsonObject>();
    JsonObject departure = temporal["departure"].to<JsonObject>();
    departure["scheduleAdvertised"] = true;
    departure["realtimeForecast"]   = true;
    departure["isCancelled"]        = true;

    JsonObject schedule = service["scheduleMetadata"].to<JsonObject>();
    schedule["modeType"]           = true;
    schedule["inPassengerService"] = true;
    schedule["operator"]["name"]   = true;

    JsonObject destination = service["destination"].add<JsonObject>();
    destination["location"]["description"] = true;
}

// One station's worth of board. Called once per station with a shared access
// token, so the two fetches cost one token exchange between them.
bool fetchStation(const char* accessToken, const char* crs, const char* displayName,
                  const char* filter, StationBoard& board)
{
    board.count = 0;
    board.ok    = false;
    snprintf(board.name, sizeof(board.name), "%s", displayName);

    WiFiClientSecure client;
    client.setInsecure();

    String url = String(RTT_HOST) + "/gb-nr/location?code=" + crs +
                 "&timeWindow=" + String(RTT_WINDOW_MINS);

    HTTPClient http;
    http.setTimeout(12000);
    if (!http.begin(client, url)) {
        Serial.printf("[trains] %s begin() failed\n", crs);
        return false;
    }
    http.addHeader("Authorization", String("Bearer ") + accessToken);

    const int status = http.GET();
    if (status != HTTP_CODE_OK) {
        Serial.printf("[trains] %s HTTP %d\n", crs, status);
        http.end();
        return false;
    }

    // getString(), not getStream() - see the note in weather.cpp. This response
    // is chunked too, and a raw-stream parse silently yields zero services.
    const String body = http.getString();
    http.end();

    JsonDocument filterDoc;
    buildFilter(filterDoc);

    JsonDocument doc;
    const DeserializationError err =
        deserializeJson(doc, body, DeserializationOption::Filter(filterDoc));

    if (err) {
        Serial.printf("[trains] %s parse failed: %s (%d bytes, heap %u)\n",
                      crs, err.c_str(), body.length(), ESP.getFreeHeap());
        return false;
    }

    // Our own display name wins: the API returns things like "Sutton (Surrey)"
    // and the station bar has a fixed width.
    if (displayName[0] == '\0') {
        const char* apiName = doc["query"]["location"]["description"] | "";
        if (apiName[0]) snprintf(board.name, sizeof(board.name), "%s", apiName);
    }

    JsonArrayConst services = doc["services"];
    uint8_t skippedNotPassenger = 0, skippedNoDeparture = 0, skippedFiltered = 0;

    for (JsonObjectConst service : services) {
        if (board.count >= MAX_DEPARTURES) break;

        // Freight and empty stock share this feed and are not things anyone
        // waiting on a platform can board.
        JsonObjectConst schedule = service["scheduleMetadata"];
        if (!(schedule["inPassengerService"] | false) ||
            strcmp(schedule["modeType"] | "", "TRAIN") != 0) {
            skippedNotPassenger++;
            continue;
        }

        // A call with no departure is an arrival-only terminus stop.
        JsonObjectConst departure = service["temporalData"]["departure"];
        const char* booked = departure.isNull() ? "" : (departure["scheduleAdvertised"] | "");
        if (booked[0] == '\0') {
            skippedNoDeparture++;
            continue;
        }

        const char* destinationName = service["destination"][0]["location"]["description"] | "";
        if (!matchesAnyOf(destinationName, filter)) {
            skippedFiltered++;
            continue;
        }

        Departure& row = board.departures[board.count];

        snprintf(row.destination, sizeof(row.destination), "%s", destinationName);
        snprintf(row.operatorName, sizeof(row.operatorName), "%s",
                 schedule["operator"]["name"] | "");
        // v2's location endpoint does not carry platform, so the field stays
        // empty and the row renderer omits the badge rather than printing a guess.
        row.platform[0] = '\0';

        isoClock(booked, row.scheduled, sizeof(row.scheduled));

        row.cancelled = departure["isCancelled"] | false;

        const char* realtime    = departure["realtimeForecast"] | "";
        const bool  hasRealtime = realtime[0] != '\0';

        row.delayed = false;
        if (row.cancelled) {
            snprintf(row.expected, sizeof(row.expected), "Cancelled");
        } else if (hasRealtime && strcmp(realtime, booked) != 0) {
            isoClock(realtime, row.expected, sizeof(row.expected));
            row.delayed = true;
        } else {
            snprintf(row.expected, sizeof(row.expected), "On time");
        }

        // Count down to when it actually leaves, not when it was meant to.
        row.minutesAway = row.cancelled ? -1
                                        : minutesUntilIso(hasRealtime ? realtime : booked);

        board.count++;
    }

    board.ok = true;
    Serial.printf("[trains] %s ok: %u shown of %u services "
                  "(skipped %u non-passenger, %u no-departure, %u wrong direction)\n",
                  crs, board.count, (unsigned)services.size(),
                  skippedNotPassenger, skippedNoDeparture, skippedFiltered);
    return true;
}

}  // namespace

bool fetchDepartures(BoardData& data)
{
    // One token for both stations: it is valid for about an hour and the two
    // fetches are seconds apart.
    char accessToken[768];
    if (!fetchAccessToken(accessToken, sizeof(accessToken))) return false;

    struct Wanted { const char* crs; const char* name; const char* filter; };
    static const Wanted wanted[MAX_STATIONS] = {
        { STATION_1_CRS, STATION_1_NAME, STATION_1_FILTER },
        { STATION_2_CRS, STATION_2_NAME, STATION_2_FILTER },
    };

    data.stationCount = MAX_STATIONS;

    // One station failing must not lose the other, so the result is OR-ed
    // rather than short-circuited - a dead Tulse Hill still leaves West
    // Dulwich on the wall.
    bool any = false;
    for (uint8_t i = 0; i < MAX_STATIONS; i++) {
        if (fetchStation(accessToken, wanted[i].crs, wanted[i].name, wanted[i].filter,
                         data.stations[i])) {
            any = true;
        }
    }

    data.trainsOk = any;
    return any;
}
