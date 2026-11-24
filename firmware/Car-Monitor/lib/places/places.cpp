#include "places.h"
#include <math.h>

bool placeNearest(const PlaceEntry* table, size_t count, float latDeg, float lonDeg,
                  float maxKm, PlaceMatch& out) {
  if (!table || count == 0) return false;
  if (!(latDeg >= -90.0f && latDeg <= 90.0f) || !(lonDeg >= -180.0f && lonDeg <= 180.0f)) return false;

  const float KM_PER_DEG = 111.195f;
  const float DEG_TO_RAD = 0.017453293f;
  const float cosLat = cosf(latDeg * DEG_TO_RAD);

  float bestKm2 = maxKm * maxKm;
  size_t best = count;                                  // count = none yet
  for (size_t i = 0; i < count; i++) {
    float dLat = (table[i].lat100 / 100.0f - latDeg) * KM_PER_DEG;
    float dLon = table[i].lon100 / 100.0f - lonDeg;
    if (dLon > 180.0f) dLon -= 360.0f;                  // across the 180 degree line
    if (dLon < -180.0f) dLon += 360.0f;
    dLon *= KM_PER_DEG * cosLat;
    float d2 = dLat * dLat + dLon * dLon;
    if (d2 <= bestKm2) { bestKm2 = d2; best = i; }
  }
  if (best == count) return false;
  out.name = table[best].name;
  out.state = table[best].state;
  out.km = sqrtf(bestKm2);
  return true;
}
