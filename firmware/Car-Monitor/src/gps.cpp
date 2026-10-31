#include <Arduino.h>
#include "gps.h"

static char   line[96];
static size_t len = 0;
static GpsFix current;
static GpsTiming timing;                      

void gpsInit() {
  Serial2.begin(9600, SERIAL_8N1, 16, 17);
}

GpsStatus gpsGetStatus() {                    
  return gpsStatus(millis(), timing);
}

bool gpsPoll(GpsFix& fix) {
  bool updated = false;
  while (Serial2.available()) {
    char c = Serial2.read();
    // removed: timing.lastByteMs = millis(); timing.anyByte = true;
    if (c == '\n') {
      line[len] = '\0';
      len = 0;
      if (nmeaChecksumOk(line)) {              // a real sentence, not noise
        timing.lastByteMs = millis();
        timing.anyByte = true;
        if (nmeaParseLine(line, current)) {
          updated = true;
          if (current.valid) {
            timing.lastFixMs = millis();
            timing.anyFix = true;
          }
        }
      }
    } else if (c != '\r') {
      if (len < sizeof(line) - 1) line[len++] = c;
      else len = 0;
    }
  }
  fix = current;
  if (gpsGetStatus() != GpsStatus::OK) {
    fix.valid = false;
    fix.speedMph = 0;
  }
  return updated;
}