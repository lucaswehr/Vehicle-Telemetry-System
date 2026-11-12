#pragma once
#include <stdint.h>
#include <stddef.h>

// A small ring buffer that remembers the most recent encoded records while no trip file is open,
// so the first seconds of a drive are not lost to the "wait for movement" delay.
// Pure code with no Arduino calls, so it runs in native tests.
constexpr uint8_t PREROLL_SLOTS      = 25;   // about 5 s of GPS records at 5 Hz
constexpr size_t  PREROLL_SLOT_BYTES = 80;   // must be >= the largest encoded record

struct PreRoll {
  uint8_t data[PREROLL_SLOTS][PREROLL_SLOT_BYTES];
  uint8_t len[PREROLL_SLOTS] = {0};
  uint8_t head = 0;    // slot the next record goes into
  uint8_t count = 0;   // how many slots hold a record (max PREROLL_SLOTS)
};

// Stores one record, overwriting the oldest when full. Returns false if it does not fit a slot.
bool preRollPush(PreRoll& p, const uint8_t* rec, size_t n);

uint8_t preRollCount(const PreRoll& p);

// idx 0 is the oldest stored record. Returns nullptr if idx is out of range; n is set to its length.
const uint8_t* preRollGet(const PreRoll& p, uint8_t idx, size_t& n);

void preRollClear(PreRoll& p);
