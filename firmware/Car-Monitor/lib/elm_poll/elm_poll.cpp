#include "elm_poll.h"

const uint8_t POLL_FAST_PIDS[POLL_FAST_COUNT] = {PID_RPM, PID_SPEED_KMH, PID_THROTTLE_PCT, PID_ENGINE_LOAD_PCT};
const uint8_t POLL_SLOW_PIDS[POLL_SLOW_COUNT] = {PID_COOLANT_C, PID_INTAKE_C, PID_FUEL_TRIM_ST_PCT, PID_FUEL_TRIM_LT_PCT};

static const uint8_t NONE_YET = 0xFF;

// Wrap-safe: "has `now` reached `t`?" works across the 49-day millis() rollover.
static bool reached(uint32_t now, uint32_t t) { return (int32_t)(now - t) >= 0; }

static int trackIndex(uint8_t pid) {
  for (uint8_t i = 0; i < POLL_FAST_COUNT; i++) if (POLL_FAST_PIDS[i] == pid) return i;
  for (uint8_t i = 0; i < POLL_SLOW_COUNT; i++) if (POLL_SLOW_PIDS[i] == pid) return POLL_FAST_COUNT + i;
  if (pid == POLL_PID_VOLTAGE) return POLL_TRACKED - 1;
  return -1;
}

static void resetSession(ElmPoll& p) {
  p.mode = PollMode::NORMAL;
  p.gapActive = false;
  p.awaiting = false;
  p.cur = {PollKind::NONE, 0};
  p.slot = 0;
  p.slowIdx = 0;
  p.voltageEver = false;      // read the voltage first thing
  p.noDataRun = 0;
  p.failRun = 0;
  for (uint8_t i = 0; i < POLL_TRACKED; i++) p.lastStatus[i] = NONE_YET;   // so the first failure is logged
}

void pollInit(ElmPoll& p, uint32_t gapMs) {
  resetSession(p);
  p.gapMs = gapMs;
  p.backoffMs = POLL_BACKOFF_START_MS;
}

uint8_t elmLogStatus(ElmStatus s) {
  if (s == ElmStatus::OK) return POLL_LOG_OK;
  if (s == ElmStatus::NO_DATA || s == ElmStatus::UNABLE_TO_CONNECT) return POLL_LOG_NO_DATA;   // the car did not answer
  return POLL_LOG_ERROR;
}

uint16_t pollVoltageMv(float volts) {
  if (!(volts > 0.0f)) return 0;
  if (volts > 65.0f) return 65000;
  return (uint16_t)(volts * 1000.0f + 0.5f);
}

bool pollNext(ElmPoll& p, uint32_t nowMs, PollRequest& req) {
  if (p.awaiting || p.mode == PollMode::DOWN) return false;
  if (p.gapActive && !reached(nowMs, p.nextReqMs)) return false;

  uint32_t vEvery = (p.mode == PollMode::NORMAL) ? POLL_VOLTAGE_EVERY_MS : POLL_KEYOFF_VOLTAGE_EVERY_MS;
  PollRequest r = {PollKind::NONE, 0};

  if (!p.voltageEver || (uint32_t)(nowMs - p.lastVoltageMs) >= vEvery) {
    r = {PollKind::VOLTAGE, POLL_PID_VOLTAGE};
    p.voltageEver = true;
    p.lastVoltageMs = nowMs;
  } else if (p.mode == PollMode::NORMAL) {
    if (p.slot < POLL_FAST_COUNT) {
      r = {PollKind::PID, POLL_FAST_PIDS[p.slot]};
      p.slot++;
    } else {
      r = {PollKind::PID, POLL_SLOW_PIDS[p.slowIdx]};
      p.slot = 0;
      p.slowIdx = (uint8_t)((p.slowIdx + 1) % POLL_SLOW_COUNT);
    }
  } else if ((uint32_t)(nowMs - p.lastProbeMs) >= POLL_KEYOFF_PROBE_EVERY_MS) {
    r = {PollKind::PID, PID_RPM};
    p.lastProbeMs = nowMs;
  }

  if (r.kind == PollKind::NONE) return false;
  p.cur = r;
  p.awaiting = true;
  req = r;
  return true;
}

// Replies that mean the adapter itself is not working (silence, garbage, bus trouble). A "?" or a refusal
// still proves the adapter is alive and talking.
static bool countsAsLinkFailure(ElmStatus s) {
  return s == ElmStatus::BAD_FORMAT || s == ElmStatus::BUS_ERROR;
}

void pollOnLinkLost(ElmPoll& p, uint32_t nowMs) {
  p.mode = PollMode::DOWN;
  p.awaiting = false;
  p.noDataRun = 0;
  p.failRun = 0;
  p.backoffMs = POLL_BACKOFF_START_MS;
  p.retryAtMs = nowMs + p.backoffMs;
}

void pollOnResult(ElmPoll& p, ElmStatus status, uint32_t nowMs, PollLog& log) {
  log.log = false; log.pid = 0; log.status = POLL_LOG_ERROR;
  if (!p.awaiting) return;                       // a result nobody asked for
  p.awaiting = false;
  PollRequest r = p.cur;
  p.gapActive = p.gapMs > 0;
  p.nextReqMs = nowMs + p.gapMs;

  log.pid = r.pid;
  log.status = elmLogStatus(status);
  int idx = trackIndex(r.pid);
  if (status == ElmStatus::OK) log.log = true;                          // real readings are always logged
  else if (idx >= 0 && p.lastStatus[idx] != log.status) log.log = true; // failures only when they change
  if (idx >= 0) p.lastStatus[idx] = log.status;

  bool isPid = (r.kind == PollKind::PID);
  if (status == ElmStatus::OK) {
    p.failRun = 0;
    if (isPid) {
      p.noDataRun = 0;
      if (p.mode == PollMode::KEY_OFF) { p.mode = PollMode::NORMAL; p.slot = 0; }   // key is back on
    }
  } else if (status == ElmStatus::NO_DATA || status == ElmStatus::UNABLE_TO_CONNECT) {
    // The adapter answered; the car did not. With the key off a J1850 car gives "UNABLE TO CONNECT"
    // (after the adapter searches for a protocol), so it counts as silence, not as a lost adapter.
    p.failRun = 0;
    if (isPid) {
      if (p.noDataRun < 255) p.noDataRun++;
      if (p.mode == PollMode::NORMAL && p.noDataRun >= POLL_KEYOFF_NODATA_RUN) {
        p.mode = PollMode::KEY_OFF;
        p.lastProbeMs = nowMs;
      }
    }
  } else if (countsAsLinkFailure(status)) {
    if (++p.failRun >= POLL_LINK_FAIL_LIMIT) pollOnLinkLost(p, nowMs);
  } else {
    p.failRun = 0;
  }
}

bool pollReconnectDue(const ElmPoll& p, uint32_t nowMs) {
  return p.mode == PollMode::DOWN && reached(nowMs, p.retryAtMs);
}

void pollOnReconnect(ElmPoll& p, bool ok, uint32_t nowMs) {
  if (p.mode != PollMode::DOWN) return;
  if (ok) {
    resetSession(p);
    p.backoffMs = POLL_BACKOFF_START_MS;
  } else {
    p.backoffMs = (p.backoffMs * 2 > POLL_BACKOFF_MAX_MS) ? POLL_BACKOFF_MAX_MS : p.backoffMs * 2;
    p.retryAtMs = nowMs + p.backoffMs;
  }
}
