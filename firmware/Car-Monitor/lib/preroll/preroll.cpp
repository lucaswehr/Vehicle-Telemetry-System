#include "preroll.h"
#include <string.h>

bool preRollPush(PreRoll& p, const uint8_t* rec, size_t n) {
  if (n == 0 || n > PREROLL_SLOT_BYTES) return false;
  memcpy(p.data[p.head], rec, n);
  p.len[p.head] = (uint8_t)n;
  p.head = (uint8_t)((p.head + 1) % PREROLL_SLOTS);
  if (p.count < PREROLL_SLOTS) p.count++;
  return true;
}

uint8_t preRollCount(const PreRoll& p) { return p.count; }

const uint8_t* preRollGet(const PreRoll& p, uint8_t idx, size_t& n) {
  if (idx >= p.count) { n = 0; return nullptr; }
  uint8_t slot = (uint8_t)((p.head + PREROLL_SLOTS - p.count + idx) % PREROLL_SLOTS);
  n = p.len[slot];
  return p.data[slot];
}

void preRollClear(PreRoll& p) { p.head = 0; p.count = 0; }
