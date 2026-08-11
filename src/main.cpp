// ---------------------------------------------------------------------------
// Weather + train departure board on a 10.2" 4-colour e-paper panel.
//
// One pass per wake: connect, fetch, draw, sleep. There is no loop() work -
// a full 4-colour refresh takes ~20 seconds and flashes the panel through its
// colour planes, so the board is deliberately a slow, quiet appliance rather
// than something that redraws continuously.
// ---------------------------------------------------------------------------
#include <Arduino.h>
#include <GxEPD2_4C.h>
#include <SPI.h>
#include <WiFi.h>
#include <esp_sleep.h>
#include <string.h>
#include <time.h>

#include "board_pins.h"
#include "config.h"
#include "model.h"
#include "render.h"
#include "trains.h"
#include "weather.h"

#if SELFTEST
#include "selftest.h"
#endif

// GxEPD2 renders in horizontal bands so the full 150 KB framebuffer never has
// to exist at once. MAX_DISPLAY_BUFFER_SIZE (see platformio.ini) sets the
// budget; this works out how many rows fit in it at 2 bits per pixel.
#define MAX_HEIGHT_4C(EPD)                                       \
    (EPD::HEIGHT <= (MAX_DISPLAY_BUFFER_SIZE / 2) / (EPD::WIDTH / 8) \
         ? EPD::HEIGHT                                           \
         : (MAX_DISPLAY_BUFFER_SIZE / 2) / (EPD::WIDTH / 8))

// The GDEM102F91 has no class of its own in GxEPD2. GDEY116F51 is the 11.6"
// panel from the same family: same 960x640 grid, same SSD2677 controller, and
// an init sequence that sends resolution from WIDTH/HEIGHT rather than
// hardcoding it - so it drives this panel unchanged. See README.
GxEPD2_4C<GxEPD2_1160c_GDEY116F51, MAX_HEIGHT_4C(GxEPD2_1160c_GDEY116F51)>
    display(GxEPD2_1160c_GDEY116F51(EPD_CS, EPD_DC, EPD_RST, EPD_BUSY));

#if EPD_USE_HSPI
SPIClass epdSpi(HSPI);
#endif

// Survives deep sleep, so a failed fetch redraws the last good board marked
// stale instead of blanking the wall.
RTC_DATA_ATTR BoardData persisted;
RTC_DATA_ATTR bool      hasPersistedData = false;

// How many partial refreshes have run back to back. Colour e-paper leaves
// residue when it is refreshed in a window, so this bounds the run before a
// full refresh clears it. Lives in RTC memory for the same reason the board
// does: it has to survive deep sleep to mean anything.
RTC_DATA_ATTR uint8_t   partialChain = 0;

namespace {

bool clockIsSet()
{
    return time(nullptr) > 1700000000;  // any plausible post-2023 timestamp
}

bool connectWiFi()
{
    Serial.printf("[wifi] connecting to %s\n", WIFI_SSID);
    WiFi.mode(WIFI_STA);
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

    const uint32_t deadline = millis() + 25000;
    while (WiFi.status() != WL_CONNECTED && millis() < deadline) {
        delay(250);
    }

    if (WiFi.status() != WL_CONNECTED) {
        Serial.println(F("[wifi] failed"));
        return false;
    }
    Serial.printf("[wifi] ok, %s\n", WiFi.localIP().toString().c_str());
    return true;
}

bool syncClock()
{
    configTzTime(TIMEZONE, NTP_SERVER_1, NTP_SERVER_2);
    const uint32_t deadline = millis() + 15000;
    while (!clockIsSet() && millis() < deadline) {
        delay(200);
    }
    if (!clockIsSet()) {
        Serial.println(F("[time] NTP sync failed"));
        return false;
    }
    return true;
}

void stampTimestamps(BoardData& data)
{
    if (!clockIsSet()) return;

    const time_t rawNow = time(nullptr);
    struct tm local;
    localtime_r(&rawNow, &local);

    strftime(data.headerDay,  sizeof(data.headerDay),  "%A",      &local);
    strftime(data.headerDate, sizeof(data.headerDate), "%e %B %Y", &local);
    strftime(data.lastUpdated, sizeof(data.lastUpdated), "%H:%M",  &local);

    // %e pads single digits with a space; drop it so the date sits flush.
    if (data.headerDate[0] == ' ') {
        memmove(data.headerDate, data.headerDate + 1, strlen(data.headerDate));
    }
}

bool inQuietHours(int hour)
{
    if (!QUIET_HOURS_ENABLED) return false;
    if (QUIET_HOUR_START == QUIET_HOUR_END) return false;
    if (QUIET_HOUR_START < QUIET_HOUR_END) {
        return hour >= QUIET_HOUR_START && hour < QUIET_HOUR_END;
    }
    return hour >= QUIET_HOUR_START || hour < QUIET_HOUR_END;  // wraps midnight
}

// The hour the board is acted on rather than glanced at. Unlike quiet hours
// this one never wraps midnight - a rush that started before midnight and
// ended after it would be a different feature - so a plain range is enough.
bool inRushHours(int hour)
{
    if (!RUSH_ENABLED) return false;
    if (RUSH_HOUR_START >= RUSH_HOUR_END) return false;
    return hour >= RUSH_HOUR_START && hour < RUSH_HOUR_END;
}

// Whether *now* is inside the rush window. Needs the clock: without it there
// is no way to know, and guessing would mean refreshing every three minutes
// around the clock.
bool rushNow()
{
    if (!clockIsSet()) return false;
    const time_t rawNow = time(nullptr);
    struct tm local;
    localtime_r(&rawNow, &local);
    return inRushHours(local.tm_hour);
}

uint32_t secondsUntilNextWake(bool fetchSucceeded)
{
    uint32_t interval = (uint32_t)(fetchSucceeded ? REFRESH_MINUTES : RETRY_MINUTES) * 60;

    if (!clockIsSet()) return interval;  // no clock, so no alignment to do

    const time_t rawNow = time(nullptr);
    struct tm local;
    localtime_r(&rawNow, &local);

    // A successful fetch inside the rush window comes back on the fast
    // cadence. A failed one keeps RETRY_MINUTES, which is already short.
    if (fetchSucceeded && inRushHours(local.tm_hour)) {
        interval = (uint32_t)RUSH_REFRESH_MINUTES * 60;
    }

    if (inQuietHours(local.tm_hour)) {
        const int hoursUntilEnd = (QUIET_HOUR_END - local.tm_hour + 24) % 24;
        const int32_t seconds =
            hoursUntilEnd * 3600 - (local.tm_min * 60 + local.tm_sec);
        Serial.printf("[sleep] quiet hours, waking in %d min\n", (int)(seconds / 60));
        return seconds < 60 ? 60u : (uint32_t)seconds;
    }

    // Align to the interval grid so refreshes land on tidy clock times rather
    // than drifting by however long each fetch happened to take.
    const uint32_t intoHour = (uint32_t)(local.tm_min * 60 + local.tm_sec);
    const uint32_t next     = ((intoHour / interval) + 1) * interval;
    uint32_t seconds = next > intoHour ? next - intoHour : interval;

    // Waking a second before a grid boundary is worse than useless: the panel
    // costs ~22s and a full colour flash per refresh, so a 1s sleep means the
    // board redraws twice back to back. Observed in the wild as
    // "[sleep] deep sleep for 1 s". Skip to the following slot instead.
    if (seconds < 60) seconds += interval;
    return seconds;
}

void initDisplay()
{
#if EPD_USE_HSPI
    // This board wires the FPC connector to HSPI pins with SCK and MOSI
    // swapped, so hardware SPI must be re-pinned before the panel is touched.
    // Skipping this is the usual reason for a board that logs success but
    // stays blank.
    epdSpi.begin(EPD_SCK, EPD_MISO, EPD_MOSI, EPD_CS);
    display.epd2.selectSPI(epdSpi, SPISettings(4000000, MSBFIRST, SPI_MODE0));
#endif
    display.init(115200, true, 2, false);
    display.setRotation(DISPLAY_ROTATION);
}

// `topRegionOnly` redraws just the masthead and the two station blocks and
// leaves the weather half of the panel exactly as it is. renderBoard() is
// still called unchanged: GxEPD2 clips every write to the active window, so
// the weather calls simply land nowhere. Keeping one render function is worth
// the handful of wasted draw calls - two renderers that had to agree about a
// boundary would drift the first time the layout moved.
void paint(const BoardData& data, bool topRegionOnly = false)
{
    if (topRegionOnly) {
        int16_t x, y, w, h;
        rushRefreshRegion(x, y, w, h);
        display.setPartialWindow(x, y, w, h);
    } else {
        display.setFullWindow();
    }

    display.firstPage();
    do {
        renderBoard(display, data);
    } while (display.nextPage());
}

#if SELFTEST
void paintPanelTest()
{
    display.setFullWindow();
    display.firstPage();
    do {
        drawPanelTest(display);
    } while (display.nextPage());
}
#endif

void paintMessage(const char* title, const char* detail)
{
    display.setFullWindow();
    display.firstPage();
    do {
        renderMessage(display, title, detail);
    } while (display.nextPage());
}

void sleepUntilNextRefresh(bool fetchSucceeded)
{
    const uint32_t seconds = secondsUntilNextWake(fetchSucceeded);
    Serial.printf("[sleep] deep sleep for %u s\n", seconds);
    Serial.flush();

    WiFi.disconnect(true);
    WiFi.mode(WIFI_OFF);
    display.hibernate();

    esp_sleep_enable_timer_wakeup((uint64_t)seconds * 1000000ULL);
    esp_deep_sleep_start();
}

}  // namespace

void setup()
{
    Serial.begin(115200);
    delay(100);
    Serial.println(F("\n[board] wake"));

#if SELFTEST
    // Panel liveness probe, before the display library touches anything.
    //
    // The vendor's own init waits on BUSY immediately after the reset pulse,
    // before any SPI command - so if BUSY never releases, the cause is upstream
    // of every protocol question (driver class, init sequence, SPI mode). This
    // reports the raw pin level and whether a reset pulse moves it, which
    // separates "panel is dead/unpowered/unplugged" from "panel is alive but we
    // are talking to it wrongly".
    pinMode(EPD_BUSY, INPUT);
    pinMode(EPD_RST, OUTPUT);
    digitalWrite(EPD_RST, HIGH);
    delay(20);
    Serial.printf("[probe] BUSY(GPIO%d) idle = %s\n", EPD_BUSY,
                  digitalRead(EPD_BUSY) ? "HIGH (ready)" : "LOW (busy/absent)");

    // A pull-up on our side: if the panel is not driving BUSY at all, this
    // reads HIGH; if the panel is actively holding it low, it stays LOW. That
    // tells absent-and-floating apart from present-and-stuck.
    pinMode(EPD_BUSY, INPUT_PULLUP);
    delay(5);
    Serial.printf("[probe] BUSY with pull-up = %s\n",
                  digitalRead(EPD_BUSY) ? "HIGH (nothing driving it)"
                                        : "LOW (actively held low)");
    pinMode(EPD_BUSY, INPUT);

    Serial.println(F("[probe] pulsing RST, sampling BUSY for 3s"));
    digitalWrite(EPD_RST, LOW);
    delay(20);
    digitalWrite(EPD_RST, HIGH);
    int last = -1;
    const uint32_t until = millis() + 3000;
    while (millis() < until) {
        const int level = digitalRead(EPD_BUSY);
        if (level != last) {
            Serial.printf("[probe]   t+%4lums BUSY -> %s\n",
                          (unsigned long)(3000 - (until - millis())),
                          level ? "HIGH" : "LOW");
            last = level;
        }
        delay(2);
    }

    // Full scan. Every pin documented for BUSY says GPIO13 - GxEPD2's example,
    // the vendor's own sketch, the web - and GPIO13 is not being driven. So
    // stop believing the documentation and ask the hardware instead.
    //
    // A pin that reads HIGH against an internal pull-down, or LOW against an
    // internal pull-up, is being actively driven by something external. Any
    // such pin is the panel talking. If nothing anywhere is driven, the panel
    // is unpowered or dead and no pin mapping will save it.
    //
    // GPIO6-11 are the SPI flash and are never touched. GPIO34-39 are
    // input-only with no internal pull resistors, so they cannot be tested
    // this way and are listed only for completeness.
    Serial.println(F("[scan] looking for any externally driven pin"));
    static const uint8_t candidates[] = {
        2, 4, 5, 12, 13, 14, 15, 16, 17, 18, 19, 21, 22, 23, 25, 26, 27, 32, 33
    };
    uint8_t drivenCount = 0;
    for (uint8_t i = 0; i < sizeof(candidates); i++) {
        const uint8_t pin = candidates[i];
        if (pin == EPD_RST) continue;   // we are driving this one ourselves

        pinMode(pin, INPUT_PULLDOWN);
        delay(4);
        const bool highAgainstPulldown = digitalRead(pin);

        pinMode(pin, INPUT_PULLUP);
        delay(4);
        const bool lowAgainstPullup = !digitalRead(pin);

        pinMode(pin, INPUT);

        if (highAgainstPulldown && !lowAgainstPullup) {
            Serial.printf("[scan]   GPIO%-2u driven HIGH  <-- candidate BUSY (ready)\n", pin);
            drivenCount++;
        } else if (lowAgainstPullup && !highAgainstPulldown) {
            Serial.printf("[scan]   GPIO%-2u driven LOW\n", pin);
            drivenCount++;
        } else if (highAgainstPulldown && lowAgainstPullup) {
            Serial.printf("[scan]   GPIO%-2u inconsistent (follows the pull - floating)\n", pin);
        }
    }
    Serial.printf("[scan] %u pin(s) externally driven\n", drivenCount);
    if (drivenCount == 0) {
        Serial.println(F("[scan] nothing is driving any pin: panel unpowered or dead"));
    }
    Serial.println(F("[probe] done"));
#endif

    initDisplay();

#if SELFTEST
    // Bring-up mode. Deliberately touches neither WiFi nor either API, so if
    // these two screens draw correctly the display, driver substitution, HSPI
    // remap and layout are all proven - and anything still wrong afterwards is
    // a network or credentials problem, not a hardware one.
    Serial.println(F("[board] SELF TEST build: no network, no fetches"));
    paintPanelTest();
    Serial.println(F("[board] panel test drawn; board layout follows in 20s"));
    delay(20000);

    BoardData demo;
    buildSelfTestData(demo);
    paint(demo);

    Serial.println(F("[board] self test complete; halting"));
    display.hibernate();
    return;
#endif

    if (!connectWiFi()) {
        if (hasPersistedData) {
            persisted.stale = true;
            paint(persisted);
        } else {
            paintMessage("No WiFi", "Check WIFI_SSID and WIFI_PASSWORD in config.h");
        }
        sleepUntilNextRefresh(false);
    }

    syncClock();

    // Start from the last good board so a partial failure keeps the half that
    // still works, rather than dropping both panels because one API was down.
    BoardData data = hasPersistedData ? persisted : BoardData{};
    data.weatherOk = false;
    data.trainsOk  = false;

    const bool weatherOk = fetchWeather(data);
    const bool trainsOk  = fetchDepartures(data);

    // Anything that failed this pass falls back to whatever the previous pass
    // stored, which is why the flags are only raised by a successful fetch.
    if (!weatherOk && hasPersistedData) {
        data.now       = persisted.now;
        data.dayCount  = persisted.dayCount;
        memcpy(data.days, persisted.days, sizeof(data.days));
        data.weatherOk = persisted.weatherOk;
    }
    if (!trainsOk && hasPersistedData) {
        data.stationCount = persisted.stationCount;
        memcpy(data.stations, persisted.stations, sizeof(data.stations));
        data.trainsOk = persisted.trainsOk;
    }

    data.stale = !(weatherOk && trainsOk);
    if (weatherOk || trainsOk) {
        stampTimestamps(data);
    }
    if (data.headerDay[0] == '\0') {
        stampTimestamps(data);
    }

    // A partial refresh is only worth taking when there is already a good board
    // on the glass to leave the bottom half of, and only for a bounded run
    // before a full one clears the residue. Outside the rush window the chain
    // resets, so the first rush wake of the morning always starts from a fully
    // refreshed panel.
    const bool rush    = rushNow();
    const bool partial = rush && hasPersistedData && partialChain < RUSH_MAX_PARTIAL_CHAIN;

    if (partial) partialChain++;
    else         partialChain = 0;

    Serial.printf("[draw] %s refresh (rush %s, partial chain %u/%u)\n",
                  partial ? "partial, top region" : "full frame",
                  rush ? "yes" : "no", partialChain, (unsigned)RUSH_MAX_PARTIAL_CHAIN);

    paint(data, partial);

    persisted        = data;
    hasPersistedData = true;

    sleepUntilNextRefresh(weatherOk && trainsOk);
}

void loop()
{
    // Never reached: setup() always ends in deep sleep, which restarts from
    // setup() on wake.
}
