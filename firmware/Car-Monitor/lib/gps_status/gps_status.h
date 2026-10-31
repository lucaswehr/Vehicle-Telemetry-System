#pragma once
#include <stdint.h>

enum class GpsStatus { LINK_LOST, NO_FIX, OK };

constexpr uint32_t GPS_LINK_TIMEOUT_MS = 3000;
constexpr uint32_t GPS_FIX_TIMEOUT_MS  = 3000;

struct GpsTiming {
  uint32_t lastByteMs = 0;   // last time any byte arrived
  uint32_t lastFixMs  = 0;   // last time a valid fix arrived
  bool anyByte = false;      // have we ever heard from the module
  bool anyFix  = false;      // have we ever had a fix

  GpsTiming() = default;
  GpsTiming(uint32_t lastByte, uint32_t lastFix, bool byteHeard, bool fixHeard)
      : lastByteMs(lastByte), lastFixMs(lastFix), anyByte(byteHeard), anyFix(fixHeard) {}
};

GpsStatus gpsStatus(uint32_t nowMs, const GpsTiming& t);