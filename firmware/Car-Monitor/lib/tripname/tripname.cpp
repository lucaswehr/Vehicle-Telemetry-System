#include "tripname.h"
#include <stdio.h>

bool tripDateValid(uint32_t ddmmyy) {
  uint32_t d = ddmmyy / 10000, m = (ddmmyy / 100) % 100, y = ddmmyy % 100;
  return d >= 1 && d <= 31 && m >= 1 && m <= 12 && y >= 24 && y <= 49;
}

bool tripName(char* out, size_t outSize, uint32_t ddmmyy, int trip) {
  if (!out || !tripDateValid(ddmmyy) || trip < 1) return false;
  uint32_t d = ddmmyy / 10000, m = (ddmmyy / 100) % 100, y = ddmmyy % 100;
  int n = snprintf(out, outSize, "/20%02u-%02u-%02u_trip%d.bin",
                   (unsigned)y, (unsigned)m, (unsigned)d, trip);
  return n > 0 && (size_t)n < outSize;
}
