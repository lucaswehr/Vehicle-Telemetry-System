#include "metrics.h"

void statRecord(Stat& s, uint32_t v) {
  s.count++;
  s.sum += v;
  if (v < s.minV) s.minV = v;
  if (v > s.maxV) s.maxV = v;
}

uint32_t statAvg(const Stat& s) {
  return s.count ? (uint32_t)(s.sum / s.count) : 0;
}

uint32_t statMin(const Stat& s) {
  return s.count ? s.minV : 0;
}

void statReset(Stat& s) {
  s = Stat();
}
