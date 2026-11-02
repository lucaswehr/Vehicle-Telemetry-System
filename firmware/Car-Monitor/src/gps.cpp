#include <Arduino.h>
#include "gps.h"
#include "ubx.h"

static uint32_t sentenceCount = 0;

static char   line[96];
static size_t len = 0;
static GpsFix current;
static GpsTiming timing;    

static void sendUbx(uint8_t cls, uint8_t id, const uint8_t* payload, uint16_t n) {
  uint8_t buf[32];
  size_t len = ubxEncode(cls, id, payload, n, buf);
  Serial2.write(buf, len);
  Serial2.flush();
  delay(50);                       // give the module time to apply it
}

static void gpsConfigure() {
  // Turn off sentences we don't parse, so 5 Hz fits at 9600 baud
  const uint8_t off[] = {0x01, 0x02, 0x03, 0x05};   // GLL, GSA, GSV, VTG
  for (uint8_t id : off) {
    const uint8_t p[] = {0xF0, id, 0, 0, 0, 0, 0, 0};   // all ports, rate 0
    sendUbx(0x06, 0x01, p, sizeof p);                    // UBX-CFG-MSG
  }
  // 200 ms measurement period = 5 Hz
  const uint8_t rate[] = {0xC8, 0x00, 0x01, 0x00, 0x01, 0x00};
  sendUbx(0x06, 0x08, rate, sizeof rate);                // UBX-CFG-RATE
}

uint32_t gpsSentenceCount() { return sentenceCount; }

void gpsInit() {
  Serial2.setRxBufferSize(1024);
  Serial2.begin(9600, SERIAL_8N1, 16, 17);
  delay(500);                      // let the module finish booting
  gpsConfigure();
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
          sentenceCount++;
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