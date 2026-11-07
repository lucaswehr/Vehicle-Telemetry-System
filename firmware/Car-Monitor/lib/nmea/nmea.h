#pragma once
#include <stdint.h>

constexpr float GPS_MIN_SPEED_MPH = 2.0f;   // below this, report 0

struct GpsFix {
  bool valid = false;
  double lat = 0, lon = 0;
  float speedMph = 0, headingDeg = 0, altM = 0;
  uint8_t sats = 0;
  uint32_t utcTime = 0;   // hhmmss
  uint32_t utcDate = 0;   // ddmmyy (UTC), 0 until the module reports one
};

bool nmeaChecksumOk(const char* line);
bool nmeaParseLine(const char* line, GpsFix& fix);