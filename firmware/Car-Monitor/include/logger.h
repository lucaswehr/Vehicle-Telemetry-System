#pragma once
#include "nmea.h"
#include "gps_status.h"
#include "sdstate.h"
#include "metrics.h"
#include "logfmt.h"

bool loggerInit();                       // tries to mount once; returns true if logging started
void loggerUpdate(const GpsFix& fix, GpsStatus st, uint32_t fixCount);
// State: ARMED = card ready, waiting for the car to move (no file yet); LOGGING = trip file open;
// UNMOUNTED = card missing or failed; FULL = out of space.
// One OBD reading from the ELM327 task (call from the main loop only). Written like a GPS record.
void loggerLogElm(const ElmRecord& e, uint32_t tMs);

SdState  loggerState();
uint32_t loggerRecords();                // records written into the log (or buffered for it)
uint32_t loggerDropped();                // records lost because the card was unavailable or a write failed
uint32_t loggerWriteErrors();
uint32_t loggerRemounts();               // successful re-mounts after a failure
uint32_t loggerMountFailures();
uint32_t loggerFreeMb();                 // estimated free space on the card, in MB

// Timing, in microseconds (see lib/metrics).
Stat loggerFlushTakeWindow();            // flush durations since the last call; resets the window
const Stat& loggerFlushTotal();          // flush durations since boot
uint32_t loggerMountMaxUs();             // longest single SD.begin()/open attempt since boot
