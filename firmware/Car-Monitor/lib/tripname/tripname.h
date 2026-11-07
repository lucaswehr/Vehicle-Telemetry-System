#pragma once
#include <stddef.h>
#include <stdint.h>

// ddmmyy (as in the NMEA RMC date field) must be plausible: year 2024-2049, valid month and day.
bool tripDateValid(uint32_t ddmmyy);

// Writes e.g. "/2026-11-07_trip3.bin". Returns false if the date is invalid or out is too small.
bool tripName(char* out, size_t outSize, uint32_t ddmmyy, int trip);
