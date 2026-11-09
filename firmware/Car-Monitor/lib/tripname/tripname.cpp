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

static bool isLeap(int y) { return (y % 4 == 0 && y % 100 != 0) || y % 400 == 0; }

static int daysInMonth(int y, int m) {
  static const int d[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
  return (m == 2 && isLeap(y)) ? 29 : d[m - 1];
}

bool utcToLocalDate(uint32_t utcDdmmyy, uint32_t utcHhmmss, int offsetHours, uint32_t& localDdmmyy) {
  if (!tripDateValid(utcDdmmyy) || utcHhmmss > 235959 || offsetHours < -23 || offsetHours > 23) return false;
  int hh = (int)(utcHhmmss / 10000), mm = (int)((utcHhmmss / 100) % 100), ss = (int)(utcHhmmss % 100);
  if (mm > 59 || ss > 59) return false;

  int d = (int)(utcDdmmyy / 10000), m = (int)((utcDdmmyy / 100) % 100), y = 2000 + (int)(utcDdmmyy % 100);
  if (d > daysInMonth(y, m)) return false;          // e.g. 31 February

  int h = hh + offsetHours;
  if (h < 0) {                                      // local time is on the previous day
    if (--d < 1) {
      if (--m < 1) { m = 12; y--; }
      d = daysInMonth(y, m);
    }
  } else if (h >= 24) {                             // local time is on the next day
    if (++d > daysInMonth(y, m)) {
      d = 1;
      if (++m > 12) { m = 1; y++; }
    }
  }
  if (y < 2000 || y > 2099) return false;
  localDdmmyy = (uint32_t)(d * 10000 + m * 100 + (y % 100));
  return true;
}

// 0 = Sunday ... 6 = Saturday (Sakamoto's method).
static int dayOfWeek(int y, int m, int d) {
  static const int t[] = {0, 3, 2, 5, 0, 3, 5, 1, 4, 6, 2, 4};
  if (m < 3) y--;
  return (y + y / 4 - y / 100 + y / 400 + t[m - 1] + d) % 7;
}

bool usDstActive(uint32_t utcDdmmyy, uint32_t utcHhmmss, int stdOffsetHours) {
  if (!tripDateValid(utcDdmmyy) || utcHhmmss > 235959 || stdOffsetHours > -4 || stdOffsetHours < -10) return false;
  int d = (int)(utcDdmmyy / 10000), m = (int)((utcDdmmyy / 100) % 100), y = 2000 + (int)(utcDdmmyy % 100);
  int hh = (int)(utcHhmmss / 10000);
  if (m < 3 || m > 11) return false;                // December to February
  if (m > 3 && m < 11) return true;                 // April to October
  int firstSunday = 1 + (7 - dayOfWeek(y, m, 1)) % 7;
  if (m == 3) {                                     // starts 2:00 local standard time, second Sunday
    int startDay = firstSunday + 7;
    int startHourUtc = 2 - stdOffsetHours;
    return d > startDay || (d == startDay && hh >= startHourUtc);
  }
  int endHourUtc = 2 - (stdOffsetHours + 1);        // ends 2:00 local daylight time, first Sunday
  return d < firstSunday || (d == firstSunday && hh < endHourUtc);
}
