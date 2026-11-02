#include <Arduino.h>
#include "gps.h"

void setup() {
  Serial.begin(115200);
  gpsInit();
}

void loop() {
  static uint32_t last = 0;
  static uint32_t lastCount = 0;
  GpsFix fix;
  gpsPoll(fix);

  if (millis() - last >= 1000) {
    last = millis();

    // sentences accepted in the last second (2 per update: RMC + GGA)
    uint32_t count = gpsSentenceCount();
    uint32_t perSec = count - lastCount;
    lastCount = count;

    switch (gpsGetStatus()) {
      case GpsStatus::OK:
        Serial.printf("OK | %.6f, %.6f | %.2f mph | %.1f ft | sats %d | %lu sent/s\n",
                      fix.lat, fix.lon, fix.speedMph,
                      fix.altM * 3.28084f, fix.sats, (unsigned long)perSec);
        break;
      case GpsStatus::NO_FIX:
        Serial.printf("NO_FIX (module talking, no position) | %lu sent/s\n",
                      (unsigned long)perSec);
        break;
      case GpsStatus::LINK_LOST:
        Serial.printf("LINK_LOST (no data from module) | %lu sent/s\n",
                      (unsigned long)perSec);
        break;
    }
  }
}