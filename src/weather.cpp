#include "weather.h"

#include <Arduino.h>
#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <time.h>

namespace {

// Open-Meteo returns local times when timezone=auto, which is what we want:
// every timestamp in the response can then be compared against localtime()
// without any offset arithmetic on our side.
String buildUrl()
{
    String url = F("https://api.open-meteo.com/v1/forecast?latitude=");
    url += String(WEATHER_LAT, 4);
    url += F("&longitude=");
    url += String(WEATHER_LON, 4);
    // No `&hourly=` block. The board no longer draws an hourly strip, and those
    // arrays were by far the largest thing in the response - 96 floats per
    // variable against 4 for a daily one. Not asking for them is the cheapest
    // available fix for the heap pressure that used to truncate this parse
    // whenever it landed next to an open TLS session.
    url += F("&current=temperature_2m,apparent_temperature,relative_humidity_2m,is_day,weather_code,wind_speed_10m");
    url += F("&daily=weather_code,temperature_2m_max,temperature_2m_min,precipitation_probability_max,sunrise,sunset");
    url += F("&timezone=auto&forecast_days=");
    url += String(FORECAST_DAYS);
    return url;
}

// Only these fields are kept while parsing. The filter still earns its keep
// now the hourly arrays are gone: the response carries units, elevation and a
// generation-time block we never look at.
void buildFilter(JsonDocument& filter)
{
    JsonObject current = filter["current"].to<JsonObject>();
    current["temperature_2m"]        = true;
    current["apparent_temperature"]  = true;
    current["relative_humidity_2m"]  = true;
    current["is_day"]                = true;
    current["weather_code"]          = true;
    current["wind_speed_10m"]        = true;

    JsonObject daily = filter["daily"].to<JsonObject>();
    daily["weather_code"]               = true;
    daily["temperature_2m_max"]         = true;
    daily["temperature_2m_min"]         = true;
    daily["precipitation_probability_max"] = true;
    daily["sunrise"]                    = true;
    daily["sunset"]                     = true;
}

// "2026-08-01T05:23" -> "05:23"
void copyClockPart(const char* isoTimestamp, char* out, size_t outSize)
{
    out[0] = '\0';
    if (!isoTimestamp || strlen(isoTimestamp) < 16) return;
    snprintf(out, outSize, "%.5s", isoTimestamp + 11);
}

void labelForDayOffset(int offset, char* out, size_t outSize)
{
    if (offset == 0) {
        snprintf(out, outSize, "Today");
        return;
    }
    time_t when = time(nullptr) + (time_t)offset * 86400;
    struct tm local;
    localtime_r(&when, &local);
    strftime(out, outSize, "%a", &local);
}

}  // namespace

const char* weatherDescription(int16_t code)
{
    switch (code) {
        case 0:  return "Clear";
        case 1:  return "Mainly clear";
        case 2:  return "Partly cloudy";
        case 3:  return "Overcast";
        case 45:
        case 48: return "Fog";
        case 51:
        case 53:
        case 55: return "Drizzle";
        case 56:
        case 57: return "Freezing drizzle";
        case 61: return "Light rain";
        case 63: return "Rain";
        case 65: return "Heavy rain";
        case 66:
        case 67: return "Freezing rain";
        case 71: return "Light snow";
        case 73: return "Snow";
        case 75: return "Heavy snow";
        case 77: return "Snow grains";
        case 80: return "Light showers";
        case 81: return "Showers";
        case 82: return "Heavy showers";
        case 85:
        case 86: return "Snow showers";
        case 95: return "Thunderstorm";
        case 96:
        case 99: return "Thunderstorm, hail";
        default: return "--";
    }
}

bool fetchWeather(BoardData& data)
{
    WiFiClientSecure client;
    // Open-Meteo is a public, unauthenticated, read-only endpoint and the ESP32
    // carries no root store, so certificate validation is skipped deliberately
    // here. See README for how to pin the CA if you would rather not.
    client.setInsecure();

    HTTPClient http;
    http.setTimeout(12000);
    if (!http.begin(client, buildUrl())) {
        Serial.println(F("[weather] begin() failed"));
        return false;
    }

    const int status = http.GET();
    if (status != HTTP_CODE_OK) {
        Serial.printf("[weather] HTTP %d\n", status);
        http.end();
        return false;
    }

    // Read the body via getString(), NOT deserializeJson(doc, http.getStream()).
    // getStream() hands back the raw socket, and this response arrives with
    // Transfer-Encoding: chunked - so that stream still carries the chunk-size
    // lines between fragments. ArduinoJson parses those as garbage and returns
    // an empty document while reporting success, which made this fetch fail
    // every cycle with nothing whatsoever in the log. getString() de-chunks.
    const String body = http.getString();
    http.end();

    JsonDocument filter;
    buildFilter(filter);

    JsonDocument doc;
    const DeserializationError err =
        deserializeJson(doc, body, DeserializationOption::Filter(filter));

    if (err) {
        Serial.printf("[weather] parse failed: %s (%d bytes, heap %u)\n",
                      err.c_str(), body.length(), ESP.getFreeHeap());
        return false;
    }

    // --- current ----------------------------------------------------------
    JsonObjectConst current = doc["current"];
    if (current.isNull()) {
        // Was a silent `return false`, which is how this managed to fail every
        // cycle without ever appearing in the log.
        Serial.println(F("[weather] response had no 'current' block"));
        return false;
    }

    data.now.temperature = current["temperature_2m"]       | 0.0f;
    data.now.feelsLike   = current["apparent_temperature"] | 0.0f;
    data.now.humidity    = current["relative_humidity_2m"] | 0;
    data.now.windKph     = current["wind_speed_10m"]       | 0.0f;
    data.now.code        = current["weather_code"]         | 0;
    data.now.isDay       = (current["is_day"] | 1) != 0;

    // --- daily ------------------------------------------------------------
    JsonObjectConst daily   = doc["daily"];
    JsonArrayConst dayCodes = daily["weather_code"];
    JsonArrayConst dayMax   = daily["temperature_2m_max"];
    JsonArrayConst dayMin   = daily["temperature_2m_min"];
    JsonArrayConst dayPop   = daily["precipitation_probability_max"];

    data.dayCount = 0;
    for (int i = 0; i < FORECAST_DAYS && i < (int)dayCodes.size(); i++) {
        WeatherDay& day = data.days[data.dayCount];
        labelForDayOffset(i, day.label, sizeof(day.label));
        day.code         = dayCodes[i] | 0;
        day.tempMax      = dayMax[i]   | 0.0f;
        day.tempMin      = dayMin[i]   | 0.0f;
        day.precipChance = dayPop[i]   | 0;
        data.dayCount++;
    }

    copyClockPart(daily["sunrise"][0] | "", data.now.sunrise, sizeof(data.now.sunrise));
    copyClockPart(daily["sunset"][0]  | "", data.now.sunset,  sizeof(data.now.sunset));

    data.weatherOk = true;
    Serial.printf("[weather] ok: %.1fC, %u days (%u bytes, heap %u)\n",
                  data.now.temperature, data.dayCount, body.length(), ESP.getFreeHeap());
    return true;
}
