#include <Arduino.h>
#include "gps.h"
#include "logger.h"

static const char* gpsLabel(GpsStatus st) {
  switch (st) {
    case GpsStatus::OK:        return "FIX OK";
    case GpsStatus::NO_FIX:    return "NO FIX";
    case GpsStatus::LINK_LOST: return "LINK LOST";
  }
  return "?";
}

void setup() {
  Serial.begin(115200);
  gpsInit();
  loggerInit();   // a failure here is no longer fatal: loggerUpdate() keeps retrying
}

void loop() {
  GpsFix fix;
  gpsPoll(fix);
  loggerUpdate(fix, gpsGetStatus(), gpsFixCount());

  static uint32_t last = 0;
  if (millis() - last >= 1000) {
    last = millis();
    Serial.printf("gps %s | sd %s | records %lu | dropped %lu | write errors %lu | remounts %lu\n",
                  gpsLabel(gpsGetStatus()),
                  loggerState() == SdState::LOGGING ? "OK" : "DOWN",
                  (unsigned long)loggerRecords(),
                  (unsigned long)loggerDropped(),
                  (unsigned long)loggerWriteErrors(),
                  (unsigned long)loggerRemounts());
  }
}
