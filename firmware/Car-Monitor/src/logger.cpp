#include <Arduino.h>
#include <SD.h>
#include <math.h>
#include <string.h>
#include "logger.h"
#include "logfmt.h"
#include "tripname.h"

static File     file;
static uint8_t  buf[512];
static size_t   used = 0;
static uint32_t seq = 0, lastFixCount = 0, lastFlushMs = 0;
static uint32_t records = 0, writeErrors = 0;
static char     path[40] = "";
static bool     renamedForDate = false;

static void flushBuf() {
  if (!file || used == 0) return;
  if (file.write(buf, used) != used) writeErrors++;
  file.flush();
  used = 0;
  lastFlushMs = millis();
}

// Once the GPS reports a date, give the trip a date-based name. Keeps all data written so far.
static void renameForDate(uint32_t ddmmyy) {
  if (renamedForDate || !tripDateValid(ddmmyy)) return;
  renamedForDate = true;                            // try once only

  char name[40];
  int n = 1;
  do {
    if (!tripName(name, sizeof name, ddmmyy, n++)) return;
  } while (SD.exists(name) && n < 1000);

  flushBuf();                                       // nothing buffered may be lost
  file.close();
  const char* use = SD.rename(path, name) ? name : path;   // on failure keep the old name
  file = SD.open(use, FILE_APPEND);
  if (!file) { writeErrors++; return; }
  if (use == name) strncpy(path, name, sizeof path - 1);
  Serial.printf("trip renamed to %s\n", path);
}

bool loggerInit() {
  if (!SD.begin(5, SPI, 4000000)) return false;     // use whichever speed passed your SD test

  char name[24];
  int n = 1;
  do {                                              // first unused trip number
    snprintf(name, sizeof name, "/trip_%04d.bin", n++);
  } while (SD.exists(name) && n < 10000);

  strncpy(path, name, sizeof path - 1);
  file = SD.open(name, FILE_WRITE);
  if (!file) return false;

  used = writeFileHeader(buf, sizeof buf);
  flushBuf();
  Serial.printf("logging to %s\n", name);
  return true;
}

void loggerUpdate(const GpsFix& fix, GpsStatus st, uint32_t fixCount) {
  if (!file) return;

  renameForDate(fix.utcDate);
  if (!file) return;

  if (fixCount != lastFixCount) {                   // a new position update arrived
    lastFixCount = fixCount;

    uint32_t t = fix.utcTime;                       // hhmmss
    GpsRecord g;
    g.latE7         = (int32_t)lround(fix.lat * 1e7);
    g.lonE7         = (int32_t)lround(fix.lon * 1e7);
    g.speedCentiMph = (uint16_t)lroundf(fix.speedMph * 100.0f);
    g.altDeciM      = (int16_t)lroundf(fix.altM * 10.0f);
    g.headingDeci   = (uint16_t)lroundf(fix.headingDeg * 10.0f);
    g.sats          = fix.sats;            
    g.utcMs         = (t / 10000) * 3600000UL + ((t / 100) % 100) * 60000UL + (t % 100) * 1000UL;

    bool good = (st == GpsStatus::OK) && fix.valid; 
    g.status = good ? 2 : (st == GpsStatus::LINK_LOST ? 0 : 1);  // 0 LINK_LOST, 1 NO_FIX, 2 OK

    if (!good) { 
      g.latE7 = 0;
      g.lonE7 = 0;
      g.speedCentiMph = 0;
      g.altDeciM = 0;
      g.headingDeci = 0;
    }

    uint8_t rec[LOG_MAX_RECORD];
    size_t n = encodeGps(g, seq++, millis(), rec, sizeof rec);
    if (n) {
      if (used + n > sizeof buf) flushBuf();
      memcpy(buf + used, rec, n);
      used += n;
      records++;
    }
  }

  if (used > 0 && millis() - lastFlushMs >= 1000) flushBuf();   // flush at least once a second
}

uint32_t loggerRecords()     { return records; }
uint32_t loggerWriteErrors() { return writeErrors; }