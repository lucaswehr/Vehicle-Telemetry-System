// The car firmware: GPS + SD logging + OBD-II (Bluetooth) + the screen, all at once.
//   pio run -e car -t upload && pio device monitor
// The ELM327 runs in its own task (src/elm327.cpp); everything else stays in this loop, so the GPS and
// logging code is the same as the baseline build (main.cpp, the "esp32dev" environment).
#include <Arduino.h>
#include <string.h>
#include "gps.h"
#include "logger.h"
#include "metrics.h"
#include "elm327.h"
#include "display_ui.h"
#include "dashmodel.h"
#include "places.h"

static DashInput dash;
static GpsFix lastFix;

static const char* sdLabel(SdState st) {
  switch (st) {
    case SdState::ARMED:     return "READY";
    case SdState::LOGGING:   return "LOGGING";
    case SdState::UNMOUNTED: return "DOWN";
    case SdState::FULL:      return "FULL";
  }
  return "?";
}

static const char* elmLabel(ElmLink l) {
  switch (l) {
    case ElmLink::OFF:        return "OFF";
    case ElmLink::CONNECTING: return "CONNECTING";
    case ElmLink::LIVE:       return "LIVE";
    case ElmLink::KEY_OFF:    return "KEY OFF";
    case ElmLink::DOWN:       return "LOST";
  }
  return "?";
}

// Moves everything the screen shows into one DashInput.
static void updateDash(uint32_t now, GpsStatus gst) {
  ElmSnapshot s;
  elmGetSnapshot(s);
  dash.obdSpeedKmh = s.speedKmh;
  dash.rpm = s.rpm;
  dash.throttlePct = s.throttlePct;
  dash.loadPct = s.loadPct;
  dash.coolantC = s.coolantC;
  dash.volts = s.volts;
  dash.elm = s.link;

  dash.gps = gst;
  dash.sats = lastFix.sats;
  bool good = (gst == GpsStatus::OK) && lastFix.valid;
  if (good) {
    sampleSet(dash.gpsSpeedMph, lastFix.speedMph, now);
    sampleSet(dash.lat, (float)lastFix.lat, now);
    sampleSet(dash.lon, (float)lastFix.lon, now);
    sampleSet(dash.altM, lastFix.altM, now);
    sampleSet(dash.headingDeg, lastFix.headingDeg, now);
  }

  dash.sd = loggerState();
  dash.sdFreeKnown = (dash.sd != SdState::UNMOUNTED);
  dash.sdFreeMb = loggerFreeMb();

  float mph;
  if (dashSpeedMph(dash, now, mph)) tripUpdate(dash.trip, mph, now);

  // The nearest town, once a second (it scans the whole table, which takes a moment).
  static uint32_t lastCity = 0;
  if (now - lastCity >= 1000) {
    lastCity = now;
    PlaceMatch m;
    if (good && placeNearest(PLACES, PLACES_COUNT, (float)lastFix.lat, (float)lastFix.lon, PLACE_MAX_KM, m)) {
      strncpy(dash.city, m.name, sizeof dash.city - 1);
      dash.city[sizeof dash.city - 1] = '\0';
      strncpy(dash.cityState, m.state, sizeof dash.cityState - 1);
      dash.cityState[sizeof dash.cityState - 1] = '\0';
      dash.cityKnown = true;
    } else {
      dash.cityKnown = false;
    }
  }
}

void setup() {
  Serial.begin(115200);
  gpsInit();
  loggerInit();          // a failure here is not fatal: loggerUpdate() keeps retrying
  displayInit();
  elmStart();
}

void loop() {
  // Loop period, measured the same way as in main.cpp so the two builds can be compared.
  static uint32_t prevStart = 0;
  static uint32_t printUs = 0;
  static Stat loopWin, loopAll;
  uint32_t start = micros();
  if (prevStart != 0) {
    uint32_t period = start - prevStart;
    period = (period > printUs) ? period - printUs : 0;
    statRecord(loopWin, period);
    statRecord(loopAll, period);
  }
  printUs = 0;
  prevStart = start;

  GpsFix fix;
  gpsPoll(fix);
  lastFix = fix;
  GpsStatus gst = gpsGetStatus();
  loggerUpdate(fix, gst, gpsFixCount());

  ElmLogItem item;                                   // OBD readings waiting to be logged
  while (elmPopLog(item)) {
    ElmRecord e;
    e.pid = item.pid; e.status = item.status; e.nbytes = item.nbytes;
    memcpy(e.raw, item.raw, sizeof e.raw);
    loggerLogElm(e, item.tMs);
  }

  uint32_t now = millis();
  updateDash(now, gst);
  displayTick(dash, now);
  if (Serial.available() && Serial.read() == 'n') displayNextPage();

  static uint32_t last = 0;
  if (now - last >= 1000) {
    last = now;
    uint32_t p0 = micros();
    Serial.printf("gps %s sats %u | sd %s free %lu MB | elm %s | records %lu dropped %lu errors %lu | elm queue drops %lu\n",
                  gst == GpsStatus::OK ? "FIX" : gst == GpsStatus::NO_FIX ? "NO FIX" : "LOST", (unsigned)lastFix.sats,
                  sdLabel(loggerState()), (unsigned long)loggerFreeMb(), elmLabel(dash.elm),
                  (unsigned long)loggerRecords(), (unsigned long)loggerDropped(), (unsigned long)loggerWriteErrors(),
                  (unsigned long)elmQueueDrops());
    Stat fw = loggerFlushTakeWindow();
    const Stat& fa = loggerFlushTotal();
    Serial.printf("perf loop us avg %lu max %lu (all-time max %lu) | flush n=%lu avg %lu max %lu (all-time max %lu)\n",
                  (unsigned long)statAvg(loopWin), (unsigned long)loopWin.maxV, (unsigned long)loopAll.maxV,
                  (unsigned long)fw.count, (unsigned long)statAvg(fw), (unsigned long)fw.maxV, (unsigned long)fa.maxV);
    statReset(loopWin);
    printUs = micros() - p0;
  }
}
