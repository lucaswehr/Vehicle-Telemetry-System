#pragma once
#include <stdint.h>

// Decides when a drive has started: the GPS must have a valid fix and report a speed above the
// threshold for several records in a row. Pure logic (no Arduino calls), so it runs in native tests.
constexpr float   START_SPEED_MPH   = 5.0f;   // strictly above this counts as moving
constexpr uint8_t START_CONSECUTIVE = 5;      // records in a row (about 1 s at 5 Hz); one noisy reading is not enough

struct MoveTrigger {
  uint8_t streak = 0;
};

// Call once per GPS record. Returns true once the condition has held for `need` records in a row
// (and keeps returning true while it continues to hold). Any bad or slow record restarts the count.
bool triggerOnRecord(MoveTrigger& t, bool fixGood, float speedMph,
                     float thresholdMph = START_SPEED_MPH, uint8_t need = START_CONSECUTIVE);

void triggerReset(MoveTrigger& t);
