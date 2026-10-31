#include "gps_status.h"

GpsStatus gpsStatus(uint32_t nowMs, const GpsTiming& t) {
  // unsigned subtraction stays correct when millis() wraps around (about every 49 days)
  if (!t.anyByte || (uint32_t)(nowMs - t.lastByteMs) > GPS_LINK_TIMEOUT_MS)
    return GpsStatus::LINK_LOST;
  if (!t.anyFix || (uint32_t)(nowMs - t.lastFixMs) > GPS_FIX_TIMEOUT_MS)
    return GpsStatus::NO_FIX;
  return GpsStatus::OK;
}