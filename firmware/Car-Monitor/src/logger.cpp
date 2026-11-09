#include <Arduino.h>
#include <SD.h>
#include <math.h>
#include <string.h>
#include "logger.h"
#include "logfmt.h"
#include "tripname.h"
#include "sdstate.h"

static const int SD_CS_PIN = 5;
static const uint32_t SD_SPI_HZ = 4000000;   // use whichever speed passed your SD test

static File     file;
static uint8_t  buf[512];
static size_t   used = 0;
static uint16_t pendingRecs = 0;             // records sitting in buf, not yet on the card
static uint32_t seq = 0, lastFixCount = 0, lastFlushMs = 0;
static uint32_t records = 0, dropped = 0, writeErrors = 0;
static char     path[40] = "";
static bool     renamedForDate = false;
static SdHealth health;

// Close everything and release the card so a later SD.begin() starts from a clean state.
static void dropCard() {
  if (file) file.close();
  SD.end();
  used = 0;
  dropped += pendingRecs;                    // whatever was buffered is lost
  records -= pendingRecs;
  pendingRecs = 0;
  Serial.println("SD: card lost, will retry");
}

// Writes the buffer. Returns false if the write failed.
static bool flushBuf() {
  if (!file || used == 0) return true;
  bool ok = (file.write(buf, used) == used);
  if (ok) {
    file.flush();
        ok = (bool)file && !file.getWriteError() && SD.exists(path);
  }
  lastFlushMs = millis();
  if (ok) {
    used = 0;
    pendingRecs = 0;
  } else {
    writeErrors++;
  }
  return ok;
}

// Handle the result of a flush: tolerate a one-off failure (the buffer is kept and retried),
// but give up on the file after SD_MAX_WRITE_FAILS in a row.
static void noteFlush(bool ok) {
  if (sdOnWriteResult(health, ok, millis())) dropCard();
}

// Mount the card, pick the first unused trip name and write the file header.
static bool mountAndOpen() {
  if (!SD.begin(SD_CS_PIN, SPI, SD_SPI_HZ)) return false;

  char name[24];
  int n = 1;
  do {                                       // first unused trip number
    snprintf(name, sizeof name, "/trip_%04d.bin", n++);
  } while (SD.exists(name) && n < 10000);

  strncpy(path, name, sizeof path - 1);
  file = SD.open(name, FILE_WRITE);
  if (!file) { SD.end(); return false; }

  used = writeFileHeader(buf, sizeof buf);
  pendingRecs = 0;
  renamedForDate = false;                    // a new file may take a date-based name
  if (!flushBuf()) { file.close(); SD.end(); return false; }
  Serial.printf("logging to %s\n", name);
  return true;
}

// Once the GPS reports a date, give the trip a date-based name. Keeps all data written so far.
static void renameForDate(uint32_t ddmmyy) {
  if (renamedForDate || !tripDateValid(ddmmyy)) return;
  renamedForDate = true;                     // try once only

  char name[40];
  int n = 1;
  do {
    if (!tripName(name, sizeof name, ddmmyy, n++)) return;
  } while (SD.exists(name) && n < 1000);

  if (!flushBuf()) { noteFlush(false); return; }   // nothing buffered may be lost
  file.close();
  const char* use = SD.rename(path, name) ? name : path;   // on failure keep the old name
  file = SD.open(use, FILE_APPEND);
  if (!file) { writeErrors++; noteFlush(false); return; }
  if (use == name) strncpy(path, name, sizeof path - 1);
  Serial.printf("trip renamed to %s\n", path);
}

bool loggerInit() {
  bool ok = mountAndOpen();
  sdOnMountResult(health, ok, millis());
  if (!ok) Serial.println("SD: mount failed, will keep retrying");
  return ok;
}

void loggerUpdate(const GpsFix& fix, GpsStatus st, uint32_t fixCount) {
  // 1. Card not available: retry with backoff. NOTE: SD.begin() blocks while it waits for a card.
  if (health.state == SdState::UNMOUNTED && sdShouldRetry(health, millis())) {
    bool ok = mountAndOpen();
    sdOnMountResult(health, ok, millis());
    if (ok) Serial.printf("SD: recovered (remount #%lu)\n", (unsigned long)health.remounts);
  }

  if (health.state == SdState::LOGGING) renameForDate(fix.utcDate);

  // 2. A new position update arrived: build the record. The sequence number always advances,
  //    so records lost during an outage show up as a sequence gap in read_trip.py.
  if (fixCount != lastFixCount) {
    lastFixCount = fixCount;

    uint32_t t = fix.utcTime;                // hhmmss
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
      if (health.state != SdState::LOGGING) {
        dropped++;                           // nowhere to put it
      } else {
        if (used + n > sizeof buf) noteFlush(flushBuf());
        if (health.state != SdState::LOGGING || used + n > sizeof buf) {
          dropped++;                         // flush failed and the buffer is still full
        } else {
          memcpy(buf + used, rec, n);
          used += n;
          pendingRecs++;
          records++;
        }
      }
    }
  }

  // 3. Flush at least once a second.
  if (health.state == SdState::LOGGING && used > 0 && millis() - lastFlushMs >= 1000)
    noteFlush(flushBuf());
}

SdState  loggerState()         { return health.state; }
uint32_t loggerRecords()       { return records; }
uint32_t loggerDropped()       { return dropped; }
uint32_t loggerWriteErrors()   { return writeErrors; }
uint32_t loggerRemounts()      { return health.remounts; }
uint32_t loggerMountFailures() { return health.mountFailures; }
