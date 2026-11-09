#pragma once
#include <stdint.h>

// Pure decision logic for SD failure handling (no Arduino/SD calls, so it runs in native tests).
//   LOGGING   : card mounted and a trip file is open
//   UNMOUNTED : card missing/failed; waiting to retry (with backoff)
enum class SdState : uint8_t { UNMOUNTED = 0, LOGGING = 1 };

constexpr uint8_t  SD_MAX_WRITE_FAILS  = 3;      // consecutive failed flushes before we give up on the file
constexpr uint32_t SD_RETRY_BASE_MS    = 1000;   // first retry delay
constexpr uint32_t SD_RETRY_MAX_MS     = 10000;  // backoff ceiling

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

// Report the result of a mount + open-file attempt.
void sdOnMountResult(SdHealth& h, bool ok, uint32_t nowMs);

// Report the result of a buffer flush. After SD_MAX_WRITE_FAILS in a row the state drops to UNMOUNTED.
// Returns true if this call caused the drop to UNMOUNTED (caller should close the file and SD.end()).
bool sdOnWriteResult(SdHealth& h, bool ok, uint32_t nowMs);
