#pragma once
#include "nmea.h"
#include "gps_status.h"

void gpsInit();
bool gpsPoll(GpsFix& fix);
GpsStatus gpsGetStatus();
uint32_t gpsSentenceCount();