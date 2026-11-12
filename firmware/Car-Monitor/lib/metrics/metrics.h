#pragma once
#include <stdint.h>

// Running statistics for one measured quantity (for example a duration in microseconds).
// Pure code with no Arduino calls, so it is unit-tested natively.
struct Stat {
  uint32_t count = 0;
  uint32_t minV = 0xFFFFFFFFu;
  uint32_t maxV = 0;
  uint64_t sum = 0;
};

void     statRecord(Stat& s, uint32_t v);
uint32_t statAvg(const Stat& s);   // 0 when there are no samples
uint32_t statMin(const Stat& s);   // 0 when there are no samples
void     statReset(Stat& s);
