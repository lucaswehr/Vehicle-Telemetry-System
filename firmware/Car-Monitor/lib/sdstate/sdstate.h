#pragma once
#include <stdint.h>

// Pure decision logic for SD handling (no Arduino/SD calls, so it runs in native tests).
//   UNMOUNTED : card missing/failed; waiting to retry (with backoff)
//   ARMED     : card mounted and checked, but no trip file yet; waiting for the car to move
//   LOGGING   : card mounted and a trip file is open
//   FULL      : out of space; logging stopped until the next power-up
enum class SdState : uint8_t { UNMOUNTED = 0, LOGGING = 1, ARMED = 2, FULL = 3 };

constexpr uint8_t  SD_MAX_WRITE_FAILS  = 3;      // consecutive failed flushes before we give up on the file
constexpr uint32_t SD_RETRY_BASE_MS    = 1000;   // first retry delay
constexpr uint32_t SD_RETRY_MAX_MS     = 10000;  // backoff ceiling

// Free-space policy. A trip uses roughly 0.6 MB per hour, so these are generous.
constexpr uint64_t SD_CLEAN_BELOW_BYTES  = 50ULL  * 1024 * 1024;  // at mount: below this, delete the oldest trips
constexpr uint64_t SD_CLEAN_TARGET_BYTES = 100ULL * 1024 * 1024;  // ... until this much is free
constexpr uint64_t SD_STOP_BELOW_BYTES   = 1ULL   * 1024 * 1024;  // below this, stop logging (FULL)

struct SdHealth {
  SdState  state = SdState::UNMOUNTED;
  uint8_t  consecutiveWriteFails = 0;
  uint32_t lastAttemptMs = 0;
  uint32_t retryDelayMs = SD_RETRY_BASE_MS;
  uint32_t remounts = 0;         // successful mounts after the first
  uint32_t mountFailures = 0;
  bool     everMounted = false;
};

// True when state is UNMOUNTED and the backoff delay has passed (millis() wraparound safe).
bool sdShouldRetry(const SdHealth& h, uint32_t nowMs);

// Report the result of a mount attempt. Success -> ARMED (no file yet).
void sdOnMountResult(SdHealth& h, bool ok, uint32_t nowMs);

// Report the result of creating the trip file while ARMED. Success -> LOGGING.
// On failure the state drops to UNMOUNTED and this returns true (caller should clean up and SD.end()).
bool sdOnFileResult(SdHealth& h, bool ok, uint32_t nowMs);

// Report the result of a buffer flush. After SD_MAX_WRITE_FAILS in a row the state drops to UNMOUNTED.
// Returns true if this call caused the drop to UNMOUNTED (caller should close the file and SD.end()).
bool sdOnWriteResult(SdHealth& h, bool ok, uint32_t nowMs);

// The card is out of space: stop logging until the next power-up.
void sdOnFull(SdHealth& h);

// Estimate of free bytes. Measured once at mount, then reduced by what we write: asking the FAT
// filesystem for free space can block for seconds on a big card, which we cannot afford while driving.
struct SdSpace {
  uint64_t freeBytes = 0;
};
void spaceInit(SdSpace& s, uint64_t freeBytes);
void spaceConsume(SdSpace& s, uint64_t bytes);          // saturates at 0
bool spaceNeedsCleanup(uint64_t freeBytes);             // true when below SD_CLEAN_BELOW_BYTES
bool spaceCleanupDone(uint64_t freeBytes);              // true when at or above SD_CLEAN_TARGET_BYTES
bool spaceIsFull(const SdSpace& s);                     // true when below SD_STOP_BELOW_BYTES
