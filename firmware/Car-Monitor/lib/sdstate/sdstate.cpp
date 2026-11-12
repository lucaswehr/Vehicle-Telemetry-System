#include "sdstate.h"

bool sdShouldRetry(const SdHealth& h, uint32_t nowMs) {
  if (h.state != SdState::UNMOUNTED) return false;
  return (uint32_t)(nowMs - h.lastAttemptMs) >= h.retryDelayMs;
}

void sdOnMountResult(SdHealth& h, bool ok, uint32_t nowMs) {
  h.lastAttemptMs = nowMs;
  if (ok) {
    if (h.everMounted) h.remounts++;
    h.everMounted = true;
    h.state = SdState::ARMED;
    h.consecutiveWriteFails = 0;
    h.retryDelayMs = SD_RETRY_BASE_MS;
  } else {
    h.state = SdState::UNMOUNTED;
    h.mountFailures++;
    uint32_t next = h.retryDelayMs * 2;
    h.retryDelayMs = (next > SD_RETRY_MAX_MS) ? SD_RETRY_MAX_MS : next;
  }
}

bool sdOnFileResult(SdHealth& h, bool ok, uint32_t nowMs) {
  if (h.state != SdState::ARMED) return false;
  if (ok) {
    h.state = SdState::LOGGING;
    h.consecutiveWriteFails = 0;
    return false;
  }
  h.state = SdState::UNMOUNTED;
  h.lastAttemptMs = nowMs;
  h.retryDelayMs = SD_RETRY_BASE_MS;
  return true;
}

bool sdOnWriteResult(SdHealth& h, bool ok, uint32_t nowMs) {
  if (h.state != SdState::LOGGING) return false;
  if (ok) { h.consecutiveWriteFails = 0; return false; }
  if (++h.consecutiveWriteFails >= SD_MAX_WRITE_FAILS) {
    h.state = SdState::UNMOUNTED;
    h.lastAttemptMs = nowMs;
    h.retryDelayMs = SD_RETRY_BASE_MS;
    return true;
  }
  return false;
}

void sdOnFull(SdHealth& h) { h.state = SdState::FULL; }

void spaceInit(SdSpace& s, uint64_t freeBytes) { s.freeBytes = freeBytes; }
void spaceConsume(SdSpace& s, uint64_t bytes) { s.freeBytes = (bytes >= s.freeBytes) ? 0 : s.freeBytes - bytes; }
bool spaceNeedsCleanup(uint64_t freeBytes) { return freeBytes < SD_CLEAN_BELOW_BYTES; }
bool spaceCleanupDone(uint64_t freeBytes) { return freeBytes >= SD_CLEAN_TARGET_BYTES; }
bool spaceIsFull(const SdSpace& s) { return s.freeBytes < SD_STOP_BELOW_BYTES; }
