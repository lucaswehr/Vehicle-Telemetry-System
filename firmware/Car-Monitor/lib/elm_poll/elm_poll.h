#pragma once
#include <stdint.h>
#include "elm_parse.h"

// Pure logic that decides WHAT to ask the car next and WHAT to log. No Bluetooth, no Arduino calls.
// The hardware layer loops:
//     if (pollNext(p, now, req)) { send req; wait for the reply or a timeout;
//                                  pollOnResult(p, status, now, log); if (log.log) write a record; }
//     if (pollMode(p) == PollMode::DOWN && pollReconnectDue(p, now)) { reconnect; pollOnReconnect(...); }
// A reply that never arrives is reported as ElmStatus::BAD_FORMAT.

enum class PollMode : uint8_t {
  NORMAL = 0,   // key on: fast group every cycle, one slow PID per cycle, battery voltage now and then
  KEY_OFF,      // the car stopped answering: voltage every few seconds plus a cheap probe for the key
  DOWN          // adapter link lost: nothing is asked until a reconnect succeeds (with backoff)
};

enum class PollKind : uint8_t { NONE = 0, PID, VOLTAGE };

struct PollRequest {
  PollKind kind;
  uint8_t  pid;     // mode 01 PID, or POLL_PID_VOLTAGE for the adapter's own voltage (ATRV)
};

// What to write to the log for the last result. `status` uses the log values below.
struct PollLog {
  bool    log;
  uint8_t pid;
  uint8_t status;   // POLL_LOG_OK / NO_DATA / ERROR; there is deliberately no value here
};

constexpr uint8_t POLL_LOG_OK      = 0;   // same numbers as ELM_STATUS_* in logfmt (checked by a test)
constexpr uint8_t POLL_LOG_NO_DATA = 1;
constexpr uint8_t POLL_LOG_ERROR   = 2;
constexpr uint8_t POLL_PID_VOLTAGE = 0xF0;   // same as ELM_PID_ADAPTER_VOLTAGE

constexpr uint8_t  POLL_FAST_COUNT = 4;
constexpr uint8_t  POLL_SLOW_COUNT = 4;
extern const uint8_t POLL_FAST_PIDS[POLL_FAST_COUNT];   // RPM, speed, throttle, engine load
extern const uint8_t POLL_SLOW_PIDS[POLL_SLOW_COUNT];   // coolant, intake air, short trim, long trim

constexpr uint32_t POLL_VOLTAGE_EVERY_MS         = 10000;   // key on
constexpr uint32_t POLL_KEYOFF_VOLTAGE_EVERY_MS  = 5000;
constexpr uint32_t POLL_KEYOFF_PROBE_EVERY_MS    = 3000;    // asks RPM to notice the key coming back
constexpr uint8_t  POLL_KEYOFF_NODATA_RUN        = 5;       // NO DATA answers in a row = key is off
constexpr uint8_t  POLL_LINK_FAIL_LIMIT          = 6;       // unusable replies in a row = link is down
constexpr uint32_t POLL_BACKOFF_START_MS         = 1000;
constexpr uint32_t POLL_BACKOFF_MAX_MS           = 30000;

constexpr uint8_t  POLL_TRACKED = 9;   // PIDs whose last logged status is remembered (8 PIDs + voltage)

struct ElmPoll {
  PollMode    mode = PollMode::NORMAL;
  uint32_t    gapMs = 0;               // minimum pause between requests (0 = as fast as the adapter answers)
  bool        gapActive = false;
  uint32_t    nextReqMs = 0;
  bool        awaiting = false;        // a request is out and its result has not been reported
  PollRequest cur = {PollKind::NONE, 0};
  uint8_t     slot = 0;                // 0..3 fast PIDs, 4 = the slow PID
  uint8_t     slowIdx = 0;
  bool        voltageEver = false;
  uint32_t    lastVoltageMs = 0;
  uint32_t    lastProbeMs = 0;
  uint8_t     noDataRun = 0;
  uint8_t     failRun = 0;
  uint8_t     lastStatus[POLL_TRACKED];   // last status seen per PID, 0xFF = none yet
  uint32_t    retryAtMs = 0;
  uint32_t    backoffMs = POLL_BACKOFF_START_MS;
};

void pollInit(ElmPoll& p, uint32_t gapMs = 0);

// The next thing to ask, if anything is due. False while a request is still out, while DOWN, during the
// gap between requests, and in KEY_OFF when neither voltage nor the probe is due yet.
bool pollNext(ElmPoll& p, uint32_t nowMs, PollRequest& req);

// Report the outcome of the request pollNext() handed out. Fills `log` with what (if anything) to record:
// every OK reading, but a failure only when it differs from that PID's last logged status.
void pollOnResult(ElmPoll& p, ElmStatus status, uint32_t nowMs, PollLog& log);

// The Bluetooth link dropped (the hardware layer noticed it directly). Goes DOWN with backoff.
void pollOnLinkLost(ElmPoll& p, uint32_t nowMs);

bool pollReconnectDue(const ElmPoll& p, uint32_t nowMs);          // only true while DOWN
void pollOnReconnect(ElmPoll& p, bool ok, uint32_t nowMs);        // ok: back to NORMAL; else backoff doubles

inline PollMode pollMode(const ElmPoll& p) { return p.mode; }

uint8_t  elmLogStatus(ElmStatus s);                  // OK -> 0, NO DATA -> 1, everything else -> 2
uint16_t pollVoltageMv(float volts);                 // rounded millivolts for the voltage record
