#pragma once
#include "nmea.h"
#include "gps_status.h"
#include "sdstate.h"

bool loggerInit();                       // tries to mount once; returns true if logging started
void loggerUpdate(const GpsFix& fix, GpsStatus st, uint32_t fixCount);
SdState  loggerState();
uint32_t loggerRecords();                // records written into the log (or buffered for it)
uint32_t loggerDropped();                // records lost because the card was unavailable or a write failed
uint32_t loggerWriteErrors();
uint32_t loggerRemounts();               // successful re-mounts after a failure
uint32_t loggerMountFailures();
