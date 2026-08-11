#pragma once

#include "model.h"

// Fetches current conditions, an hourly strip and a multi-day forecast from
// Open-Meteo (no API key required) and fills the weather half of `data`.
// Returns false and leaves `data` untouched on any network or parse failure.
bool fetchWeather(BoardData& data);

// Human-readable one-liner for a WMO weather code, e.g. "Light rain".
const char* weatherDescription(int16_t code);
