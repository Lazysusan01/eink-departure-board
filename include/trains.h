#pragma once

#include "model.h"

// Fetches the live departure board for STATION_CRS from RealTimeTrains and
// fills the train half of `data`. Returns false and leaves `data` untouched on
// any network or parse failure.
bool fetchDepartures(BoardData& data);
