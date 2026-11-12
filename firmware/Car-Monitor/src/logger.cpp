#include <Arduino.h>
#include <SD.h>
#include <math.h>
#include <string.h>
#include "logger.h"
#include "logfmt.h"
#include "tripname.h"
#include "sdstate.h"
#include "metrics.h"
#include "movetrigger.h"
#include "preroll.h"

static const int SD_CS_PIN = 5;
static const uint32_t SD_SPI_HZ = 4000000;   // use whichever speed passed your SD test

// File names use local dates. The GPS reports UTC; this is the zone's standard-time offset in whole
// hours (-8 = Pacific). US daylight saving time (+1 hour) is applied automatically from the GPS date.
static const int LOCAL_STD_OFFSET_HOURS = -8;

// A trip file is only created once the car is really moving (see lib/movetrigger: a valid fix and a
// speed above START_SPEED_MPH for several records in a row). For bench testing at a desk, build with
// -DLOG_WITHOUT_MOVEMENT (the "bench" environment in platformio.ini) to start logging right away.
#ifdef LOG_WITHOUT_MOVEMENT
static const bool START_WITHOUT_MOVEMENT = true;
#else
static const bool START_WITHOUT_MOVEMENT = false;
#endif

static_assert(LOG_MAX_RECORD <= PREROLL_SLOT_BYTES, "pre-roll slots must hold the largest record");

static File     file;
static uint8_t  buf[512];
static size_t   used = 0;
static uint16_t pendingRecs = 0;             // records sitting in buf, not yet on the card
static uint32_t seq = 0, lastFixCount = 0, lastFlushMs = 0;
static uint32_t records = 0, dropped = 0, writeErrors = 0;
static char     path[40] = "";
static SdHealth health;
static SdSpace  space;
static MoveTrigger trigger;
static PreRoll  preroll;                     // the last few seconds, kept while no file is open

// Timing (microseconds). The window stat is reset each time main reads it; the total never is.
static Stat     flushWin, flushAll;
static uint32_t mountMaxUs = 0;

// Close everything and release the card so a later SD.begin() starts from a clean state.
static void dropCard() {
  if (file) file.close();
  SD.end();
  used = 0;
  dropped += pendingRecs;                    // whatever was buffered is lost
  records -= pendingRecs;
  pendingRecs = 0;
  triggerReset(trigger);
  preRollClear(preroll);
  Serial.println("SD: card lost, will retry");
}

// Writes the buffer. Returns false if the write failed.
static bool flushBuf() {
  if (!file || used == 0) return true;
  uint32_t t0 = micros();
  size_t bytes = used;
  bool ok = (file.write(buf, used) == used);
  if (ok) {
    file.flush();
    ok = (bool)file && !file.getWriteError() && SD.exists(path);
  }
  lastFlushMs = millis();
  if (ok) {
    spaceConsume(space, bytes);
    used = 0;
    pendingRecs = 0;
  } else {
    writeErrors++;
  }
  uint32_t dt = micros() - t0;
  statRecord(flushWin, dt);
  statRecord(flushAll, dt);
  return ok;
}

// Out of space: close the file and stop until the next power-up.
static void stopFull() {
  if (file) file.close();
  SD.end();
  sdOnFull(health);
  Serial.println("SD: card full, logging stopped");
}

// Handle the result of a flush: tolerate a one-off failure (the buffer is kept and retried),
// but give up on the file after SD_MAX_WRITE_FAILS in a row.
static void noteFlush(bool ok) {
  if (sdOnWriteResult(health, ok, millis())) { dropCard(); return; }
  if (ok && health.state == SdState::LOGGING && spaceIsFull(space)) stopFull();
}

// Deletes the oldest trip files (and only files that match this logger's trip names) until enough
// space is free or none are left. Runs at mount time, when the car is not yet moving.
static uint64_t freeSpaceByDeletingOldest(uint64_t freeBytes) {
  for (int pass = 0; pass < 500 && !spaceCleanupDone(freeBytes); pass++) {
    File root = SD.open("/");
    if (!root) break;
    char oldest[48] = "";
    uint32_t oldestKey = 0xFFFFFFFFu, oldestSize = 0;
    for (File f = root.openNextFile(); f; f = root.openNextFile()) {
      uint32_t key;
      if (!f.isDirectory() && parseTripName(f.name(), key) && key < oldestKey) {
        const char* nm = f.name();
        snprintf(oldest, sizeof oldest, "/%s", nm[0] == '/' ? nm + 1 : nm);
        oldestKey = key;
        oldestSize = (uint32_t)f.size();
      }
      f.close();
    }
    root.close();
    if (oldest[0] == '\0') break;            // nothing left that we may delete
    if (!SD.remove(oldest)) break;
    Serial.printf("SD: low space, deleted %s\n", oldest);
    freeBytes += oldestSize;
  }
  return freeBytes;
}

// Times a mount attempt: SD.begin() blocks while it looks for the card, which stalls the loop.
static bool mountCard();
static bool timedMount() {
  uint32_t t0 = micros();
  bool ok = mountCard();
  uint32_t dt = micros() - t0;
  if (dt > mountMaxUs) mountMaxUs = dt;
  return ok;
}

// Mount the card and measure free space (deleting the oldest trips if it is nearly full).
// No file is created here: that waits for the car to move.
static bool mountCard() {
  if (!SD.begin(SD_CS_PIN, SPI, SD_SPI_HZ)) return false;
  uint64_t freeBytes = SD.totalBytes() - SD.usedBytes();
  if (spaceNeedsCleanup(freeBytes)) freeBytes = freeSpaceByDeletingOldest(freeBytes);
  spaceInit(space, freeBytes);
  Serial.printf("SD: ready, about %lu MB free\n", (unsigned long)(freeBytes >> 20));
  return true;
}

// Picks the file name: the local date plus the first unused trip number, for example
// /2026-11-09_trip2.bin. Falls back to /trip_0001.bin style if the GPS has no usable date.
static void chooseTripPath(char* out, size_t cap, const GpsFix& fix) {
  uint32_t ddmmyy;
  int offset = LOCAL_STD_OFFSET_HOURS + (usDstActive(fix.utcDate, fix.utcTime, LOCAL_STD_OFFSET_HOURS) ? 1 : 0);
  if (utcToLocalDate(fix.utcDate, fix.utcTime, offset, ddmmyy)) {
    for (int n = 1; n < 1000; n++) {
      if (!tripName(out, cap, ddmmyy, n)) break;
      if (!SD.exists(out)) return;
    }
  }
  for (int n = 1; n < 10000; n++) {
    snprintf(out, cap, "/trip_%04d.bin", n);
    if (!SD.exists(out)) return;
  }
}

// Creates the trip file, writes the header and then the pre-roll (the last few seconds). Returns false on failure.
static bool openTripFile(const GpsFix& fix) {
  char name[40];
  chooseTripPath(name, sizeof name, fix);
  file = SD.open(name, FILE_WRITE);
  if (!file) return false;
  strncpy(path, name, sizeof path - 1);
  path[sizeof path - 1] = '\0';

  used = writeFileHeader(buf, sizeof buf);
  pendingRecs = 0;
  for (uint8_t i = 0; i < preRollCount(preroll); i++) {
    size_t n = 0;
    const uint8_t* r = preRollGet(preroll, i, n);
    if (used + n > sizeof buf && !flushBuf()) return false;
    memcpy(buf + used, r, n);
    used += n;
    pendingRecs++;
    records++;
  }
  if (!flushBuf()) return false;
  preRollClear(preroll);
  Serial.printf("logging to %s\n", path);
  return true;
}

bool loggerInit() {
  bool ok = timedMount();
  sdOnMountResult(health, ok, millis());
  if (!ok) Serial.println("SD: mount failed, will keep retrying");
  else if (spaceIsFull(space)) stopFull();
  return ok;
}

void loggerUpdate(const GpsFix& fix, GpsStatus st, uint32_t fixCount) {
  // 1. Card not available: retry with backoff. NOTE: SD.begin() blocks while it waits for a card.
  if (health.state == SdState::UNMOUNTED && sdShouldRetry(health, millis())) {
    bool ok = timedMount();
    sdOnMountResult(health, ok, millis());
    if (ok) {
      Serial.printf("SD: recovered (remount #%lu)\n", (unsigned long)health.remounts);
      if (spaceIsFull(space)) stopFull();
    }
  }

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
      bool moving = START_WITHOUT_MOVEMENT || (good && fix.speedMph > START_SPEED_MPH);

      if (health.state == SdState::LOGGING) {
        if (used + n > sizeof buf) noteFlush(flushBuf());
        if (health.state != SdState::LOGGING || used + n > sizeof buf) {
          dropped++;                         // flush failed and the buffer is still full
        } else {
          memcpy(buf + used, rec, n);
          used += n;
          pendingRecs++;
          records++;
        }
      } else {
        // No file open. Keep the last few seconds so the start of the drive is not lost.
        preRollPush(preroll, rec, n);
        if (health.state == SdState::ARMED) {
          bool go = START_WITHOUT_MOVEMENT ||
                    triggerOnRecord(trigger, good, fix.speedMph);
          if (go) {
            bool ok = openTripFile(fix);
            if (sdOnFileResult(health, ok, millis())) {
              dropCard();                    // could not create the file: release the card and retry the mount
            } else if (spaceIsFull(space)) {
              stopFull();
            }
          }
        } else if (moving) {
          dropped++;                         // driving, but no card (UNMOUNTED) or no space (FULL)
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
uint32_t loggerFreeMb()        { return (uint32_t)(space.freeBytes >> 20); }

Stat loggerFlushTakeWindow() {
  Stat out = flushWin;
  statReset(flushWin);
  return out;
}
const Stat& loggerFlushTotal() { return flushAll; }
uint32_t loggerMountMaxUs()    { return mountMaxUs; }
