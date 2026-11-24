#pragma once
#include <stdint.h>
#include <stddef.h>
#include "gps_status.h"
#include "sdstate.h"

// Pure logic that turns the latest readings into the text and alert colors for each dashboard page.
// No screen code and no Arduino calls, so it is unit-tested on the PC. The screen code only draws
// what this produces.
//
// The one rule that matters: a reading that is missing or stale shows "--" (Level::NONE), never 0.

enum class Level : uint8_t {
  NONE = 0,   // no data: draw grey
  OK,         // normal: green/white
  INFO,       // neutral state (waiting, key off): blue
  WARN,       // amber
  BAD         // red
};

enum class ElmLink : uint8_t { OFF = 0, CONNECTING, LIVE, KEY_OFF, DOWN };

// ---------- a reading with a timestamp ----------
struct Sample {
  float    v = 0;
  uint32_t atMs = 0;
  bool     have = false;
};
void sampleSet(Sample& s, float v, uint32_t nowMs);
bool sampleFresh(const Sample& s, uint32_t nowMs, uint32_t maxAgeMs);   // wrap-safe

// How old a reading may be before it is shown as "--".
constexpr uint32_t DASH_OBD_MAX_AGE_MS  = 2000;    // polled several times a second
constexpr uint32_t DASH_VOLT_MAX_AGE_MS = 15000;   // read every 10 s (every 5 s with the key off)
constexpr uint32_t DASH_GPS_MAX_AGE_MS  = 3000;

// ---------- alert thresholds (change here) ----------
constexpr float DASH_COOLANT_WARN_F      = 225.0f;
constexpr float DASH_COOLANT_BAD_F       = 240.0f;
constexpr float DASH_RPM_WARN            = 5500.0f;
constexpr float DASH_RPM_BAD             = 6200.0f;
constexpr float DASH_ENGINE_RUNNING_RPM  = 400.0f;
constexpr float DASH_VOLT_RUN_WARN       = 13.0f;  // engine running: below this the alternator is weak
constexpr float DASH_VOLT_RUN_BAD        = 12.0f;
constexpr float DASH_VOLT_RUN_HIGH       = 15.0f;  // above this the regulator is overcharging
constexpr float DASH_VOLT_OFF_WARN       = 12.2f;  // engine off: battery getting low
constexpr float DASH_VOLT_OFF_BAD        = 11.8f;
constexpr uint8_t DASH_SATS_OK           = 5;
constexpr uint8_t DASH_SATS_WARN         = 3;
constexpr uint32_t DASH_SD_FREE_WARN_MB  = 200;
constexpr uint32_t DASH_SD_FREE_BAD_MB   = 50;

// ---------- trip statistics ----------
constexpr float DASH_TRIP_START_MPH = 3.0f;        // the trip clock starts the first time we exceed this

struct TripStats {
  float    miles = 0;
  float    topMph = 0;
  bool     started = false;
  uint32_t startMs = 0;
  bool     haveLast = false;
  uint32_t lastMs = 0;
};
// Feed the current speed whenever it is known. Distance is speed x time, ignoring gaps over 2 s.
void tripUpdate(TripStats& t, float mph, uint32_t nowMs);
void tripReset(TripStats& t);

// ---------- everything the pages need ----------
struct DashInput {
  Sample obdSpeedKmh, gpsSpeedMph, rpm, throttlePct, loadPct, coolantC, volts;
  Sample altM, lat, lon, headingDeg;
  GpsStatus gps = GpsStatus::LINK_LOST;
  uint8_t   sats = 0;
  SdState   sd = SdState::UNMOUNTED;
  bool      sdFreeKnown = false;
  uint32_t  sdFreeMb = 0;
  ElmLink   elm = ElmLink::OFF;
  bool      cityKnown = false;
  char      city[24] = {0};
  TripStats trip;
};

// ---------- the pages ----------
constexpr uint8_t DASH_PAGES      = 3;      // 0 = DRIVE, 1 = LOCATION, 2 = SYSTEM
constexpr uint8_t DASH_MAX_FIELDS = 8;

struct DashField {
  char  label[12];
  char  value[18];
  char  unit[6];
  Level level;
};

struct DashPage {
  const char* title;
  uint8_t     count;
  DashField   f[DASH_MAX_FIELDS];
};

void dashBuild(const DashInput& in, uint32_t nowMs, uint8_t page, DashPage& out);

// ---------- helpers (public so they can be tested directly) ----------
bool  dashSpeedMph(const DashInput& in, uint32_t nowMs, float& mph);       // OBD first, then GPS
bool  dashEngineRunning(const DashInput& in, uint32_t nowMs);
Level levelCoolantF(float f);
Level levelRpm(float rpm);
Level levelVoltage(float volts, bool engineRunning);
Level levelSats(uint8_t sats);
float cToF(float c);
float mToFt(float m);
const char* compassPoint(float deg);                                         // "N", "NE", ... "NW"
