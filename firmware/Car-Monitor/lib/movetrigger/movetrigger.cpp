#include "movetrigger.h"

bool triggerOnRecord(MoveTrigger& t, bool fixGood, float speedMph, float thresholdMph, uint8_t need) {
  if (fixGood && speedMph > thresholdMph) {
    if (t.streak < 255) t.streak++;
  } else {
    t.streak = 0;
  }
  return t.streak >= need;
}

void triggerReset(MoveTrigger& t) { t.streak = 0; }
