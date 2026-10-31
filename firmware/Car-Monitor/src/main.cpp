#include <Arduino.h>
#include "gps.h"

void setup() {
  Serial.begin(115200);
  gpsInit();
}

void loop() {
  static uint32_t last = 0;
  GpsFix fix;
  gpsPoll(fix);
  if (millis() - last >= 1000) {
    last = millis();
    switch (gpsGetStatus()) {
      case GpsStatus::OK:
        Serial.printf("OK  %.6f, %.6f  %.2f mph  satellites %d\n",
                      fix.lat, fix.lon, fix.speedMph, fix.sats);
        break;
      case GpsStatus::NO_FIX:    Serial.println("NO_FIX (module talking, no position)"); break;
      case GpsStatus::LINK_LOST: Serial.println("LINK_LOST (no data from module)");      break;
    }
  }
}