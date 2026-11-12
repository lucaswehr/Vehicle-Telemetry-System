#include <Arduino.h>
#include "gps.h"
#include "logger.h"
#include "metrics.h"

static const char* gpsLabel(GpsStatus st) {
  switch (st) {
    case GpsStatus::OK:        return "FIX OK";
    case GpsStatus::NO_FIX:    return "NO FIX";
    case GpsStatus::LINK_LOST: return "LINK LOST";
  }
  return "?";
}

static const char* sdLabel(SdState st) {
  switch (st) {
    case SdState::ARMED:     return "READY (waiting for movement)";
    case SdState::LOGGING:   return "LOGGING";
    case SdState::UNMOUNTED: return "DOWN";
    case SdState::FULL:      return "FULL";
  }
  return "?";
}

void setup() {
  Serial.begin(115200);
  gpsInit();
  loggerInit();   // a failure here is no longer fatal: loggerUpdate() keeps retrying
}

void loop() {
  // Loop period: time from the start of the previous pass to the start of this one.
  static uint32_t prevStart = 0;
  static uint32_t printUs = 0;             // time the last print took; subtracted from the next sample
  static Stat loopWin, loopAll;
  uint32_t start = micros();
  if (prevStart != 0) {
    uint32_t period = start - prevStart;
    period = (period > printUs) ? period - printUs : 0;   // don't count the serial printing itself
    statRecord(loopWin, period);
    statRecord(loopAll, period);
  }
  printUs = 0;
  prevStart = start;

  GpsFix fix;
  gpsPoll(fix);
  loggerUpdate(fix, gpsGetStatus(), gpsFixCount());

  static uint32_t last = 0;
  if (millis() - last >= 1000) {
    last = millis();
    uint32_t p0 = micros();
    Serial.printf("gps %s | sd %s | free %lu MB | records %lu | dropped %lu | write errors %lu | remounts %lu\n",
                  gpsLabel(gpsGetStatus()),
                  sdLabel(loggerState()),
                  (unsigned long)loggerFreeMb(),
                  (unsigned long)loggerRecords(),
                  (unsigned long)loggerDropped(),
                  (unsigned long)loggerWriteErrors(),
                  (unsigned long)loggerRemounts());

    Stat fw = loggerFlushTakeWindow();
    const Stat& fa = loggerFlushTotal();
    Serial.printf("perf loop us avg %lu max %lu (all-time max %lu) | flush n=%lu avg %lu max %lu (all-time max %lu) | mount max %lu ms\n",
                  (unsigned long)statAvg(loopWin), (unsigned long)loopWin.maxV, (unsigned long)loopAll.maxV,
                  (unsigned long)fw.count, (unsigned long)statAvg(fw), (unsigned long)fw.maxV, (unsigned long)fa.maxV,
                  (unsigned long)(loggerMountMaxUs() / 1000));
    statReset(loopWin);
    printUs = micros() - p0;
  }
}
