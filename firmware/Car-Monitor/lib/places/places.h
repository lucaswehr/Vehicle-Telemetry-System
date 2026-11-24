#pragma once
#include <stdint.h>
#include <stddef.h>

// Offline "what town am I in": the nearest entry of a table built by tools/make_places.py.
// Pure logic with no Arduino calls, so it is unit-tested on the PC. No internet is needed.

struct PlaceEntry {
  int16_t     lat100;    // latitude  x 100 (hundredths of a degree, about 1.1 km)
  int16_t     lon100;    // longitude x 100
  const char* name;      // plain ASCII
  const char* state;     // two-letter state code, or "" if unknown
};

struct PlaceMatch {
  const char* name;
  const char* state;
  float       km;        // distance from the query point to the place
};

constexpr float PLACE_MAX_KM = 24.0f;      // about 15 miles; farther than this shows "--" instead of a wrong name

// The nearest place within maxKm of (lat, lon), or false if there is none (or the table is empty).
// `out` is left untouched on false. Distances use a flat-earth approximation, accurate to well under
// 1% over these ranges.
bool placeNearest(const PlaceEntry* table, size_t count, float latDeg, float lonDeg,
                  float maxKm, PlaceMatch& out);

// The generated table (lib/places/places_data.cpp).
extern const PlaceEntry PLACES[];
extern const size_t PLACES_COUNT;
