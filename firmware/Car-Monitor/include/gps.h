#pragma once
#include "nmea.h"
#include "gps_status.h"

void gpsInit();
bool gpsPoll(GpsFix& fix);
GpsStatus gpsGetStatus();
uint32_t gpsSentenceCount();
uint32_t gpsFixCount();      // increments once per RMC sentence (one per position update)