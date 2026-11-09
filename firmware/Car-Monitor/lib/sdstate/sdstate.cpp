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
    h.state = SdState::LOGGING;
    h.consecutiveWriteFails = 0;
    h.retryDelayMs = SD_RETRY_BASE_MS;
  } else {
    h.state = SdState::UNMOUNTED;
    h.mountFailures++;
    uint32_t next = h.retryDelayMs * 2;
    h.retryDelayMs = (next > SD_RETRY_MAX_MS) ? SD_RETRY_MAX_MS : next;
  }
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
