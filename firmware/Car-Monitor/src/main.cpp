#include <Arduino.h>
#include "gps.h"
#include "logger.h"

void setup() {
  Serial.begin(115200);
  gpsInit();
  if (!loggerInit()) Serial.println("LOGGER INIT FAILED (check SD wiring and card)");
}

void loop() {
  GpsFix fix;
  gpsPoll(fix);
  loggerUpdate(fix, gpsGetStatus(), gpsFixCount());

  static uint32_t last = 0;
  if (millis() - last >= 1000) {
    last = millis();
    Serial.printf("status %d | records %lu | write errors %lu\n",
                  (int)gpsGetStatus(),
                  (unsigned long)loggerRecords(),
                  (unsigned long)loggerWriteErrors());
  }
}