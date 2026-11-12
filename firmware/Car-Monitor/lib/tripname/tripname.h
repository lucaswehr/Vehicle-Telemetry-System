#pragma once
#include <stddef.h>
#include <stdint.h>

// ddmmyy (as in the NMEA RMC date field) must be plausible: year 2024-2049, valid month and day.
bool tripDateValid(uint32_t ddmmyy);

// Writes e.g. "/2026-11-07_trip3.bin". Returns false if the date is invalid or out is too small.
bool tripName(char* out, size_t outSize, uint32_t ddmmyy, int trip);

// Converts a UTC date (ddmmyy) and time (hhmmss), as the GPS reports them, to the local calendar
// date for a fixed UTC offset in whole hours (for example -8 for Pacific Standard Time).
// Handles rolling back or forward across month, year and leap-day boundaries.
// Returns false for an invalid date or time, or an offset outside -23..23.
bool utcToLocalDate(uint32_t utcDdmmyy, uint32_t utcHhmmss, int offsetHours, uint32_t& localDdmmyy);

// True if US daylight saving time is in effect at the given UTC date (ddmmyy) and time (hhmmss).
// stdOffsetHours is the zone's standard-time offset, a whole number from -10 to -4
// (-8 Pacific, -7 Mountain, -6 Central, -5 Eastern). US rule: daylight time starts at 2:00 am
// local standard time on the second Sunday in March and ends at 2:00 am local daylight time on
// the first Sunday in November. Returns false for an invalid date or time.
bool usDstActive(uint32_t utcDdmmyy, uint32_t utcHhmmss, int stdOffsetHours);

// Recognizes the file names this logger creates: "/2026-11-07_trip3.bin" (with or without the leading
// slash) and the older "/trip_0001.bin". On success sets sortKey so that a smaller key means an older
// trip (undated names sort before every dated one). Anything else, such as a file you copied onto the
// card yourself, is rejected, so the cleanup can never delete it.
bool parseTripName(const char* name, uint32_t& sortKey);

