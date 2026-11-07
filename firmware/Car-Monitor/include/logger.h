#pragma once
#include "nmea.h"
#include "gps_status.h"

bool loggerInit();
void loggerUpdate(const GpsFix& fix, GpsStatus st, uint32_t fixCount);
uint32_t loggerRecords();
uint32_t loggerWriteErrors();