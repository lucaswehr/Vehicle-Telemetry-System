#include "nmea.h"

#include <cstdlib>
#include <cstring>

static int hexVal(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  return -1;
}

bool nmeaChecksumOk(const char* line) {
  if (!line || line[0] != '$') return false;
  uint8_t sum = 0;
  const char* p = line + 1;
  while (*p && *p != '*') { sum ^= (uint8_t)*p; p++; }
  if (*p != '*') return false;
  int hi = hexVal(p[1]);
  int lo = (hi >= 0) ? hexVal(p[2]) : -1;
  if (hi < 0 || lo < 0) return false;
  return sum == (uint8_t)((hi << 4) | lo);
}

static int splitFields(char* s, char* fields[], int maxFields) {
  int count = 0;
  if (!s || maxFields <= 0) return count;

  fields[count++] = s;
  for (char* p = s; *p && count < maxFields; ++p) {
    if (*p == ',') {
      *p = '\0';
      fields[count++] = p + 1;
    }
  }
  return count;
}

static double toDegrees(const char* f, char hemi) {
  if (!f[0]) return 0;
  double v = std::atof(f);
  int deg = (int)(v / 100);
  double d = deg + (v - deg * 100) / 60.0;
  return (hemi == 'S' || hemi == 'W') ? -d : d;
}

bool nmeaParseLine(const char* line, GpsFix& fix) {
  if (!nmeaChecksumOk(line)) return false;

  char buf[96];
  std::size_t len = std::strlen(line);
  if (len >= sizeof(buf)) return false;
  std::memcpy(buf, line, len + 1);
  char* checksumSeparator = std::strchr(buf, '*');
  if (!checksumSeparator) return false;
  *checksumSeparator = '\0';

  char* f[16];
  int n = splitFields(buf + 1, f, 16);
  if (n == 0 || std::strlen(f[0]) < 5) return false;
  const char* type = f[0] + 2;

  if (std::strcmp(type, "RMC") == 0 && n >= 10) {
    fix.valid = (f[2][0] == 'A');
    if (fix.valid) {
      fix.lat        = toDegrees(f[3], f[4][0]);
      fix.lon        = toDegrees(f[5], f[6][0]);
      float mph      = std::atof(f[7]) * 1.15078f;
      fix.speedMph   = (mph < GPS_MIN_SPEED_MPH) ? 0.0f : mph;
      fix.headingDeg = std::atof(f[8]);
      fix.utcTime    = static_cast<uint32_t>(std::atol(f[1]));
    } else {
      fix.speedMph = 0.0f;   // no fix means no speed, so never keep a stale value
    }
    return true;
  }
  if (std::strcmp(type, "GGA") == 0 && n >= 10) {
    fix.sats = static_cast<uint8_t>(std::atoi(f[7]));
    fix.altM = std::atof(f[9]);
    return true;
  }
  return false;
}