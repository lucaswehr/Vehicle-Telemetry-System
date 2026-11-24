#pragma once
#include <stdint.h>
#include <stddef.h>

// Pure logic for talking to an ELM327 OBD-II adapter: no Bluetooth or Arduino calls, so it runs in
// native tests. The hardware layer (BLE or Bluetooth Classic) only moves text in and out.

// ---------- Decoding a mode 01 reply ----------

enum class ElmStatus : uint8_t {
  OK = 0,
  NO_DATA,            // "NO DATA": the car did not answer (PID not supported, or engine/ignition off)
  UNABLE_TO_CONNECT,  // "UNABLE TO CONNECT": the adapter could not find a protocol
  BUS_ERROR,          // "BUS INIT: ...ERROR", "CAN ERROR", "BUS BUSY", "BUFFER FULL" and similar
  UNKNOWN_COMMAND,    // "?": the adapter did not understand the command
  NEGATIVE_RESPONSE,  // 7F xx yy: the car refused the request
  WRONG_PID,          // a valid reply, but for a different PID than the one asked (stale data)
  UNSUPPORTED_PID,    // this library has no decoder for that PID
  BAD_FORMAT          // empty, garbled or too short
};

// PIDs we decode (mode 01). Which of these a given car answers varies; ask it with elmParseSupported().
constexpr uint8_t PID_ENGINE_LOAD_PCT   = 0x04;   // calculated engine load, percent
constexpr uint8_t PID_COOLANT_C         = 0x05;   // engine coolant temperature, degrees C
constexpr uint8_t PID_FUEL_TRIM_ST_PCT  = 0x06;   // short-term fuel trim, bank 1, percent (-100..+99.2)
constexpr uint8_t PID_FUEL_TRIM_LT_PCT  = 0x07;   // long-term fuel trim, bank 1, percent
constexpr uint8_t PID_MAP_KPA           = 0x0B;   // intake manifold absolute pressure, kPa
constexpr uint8_t PID_RPM               = 0x0C;   // engine speed, rpm
constexpr uint8_t PID_SPEED_KMH         = 0x0D;   // vehicle speed, km/h
constexpr uint8_t PID_TIMING_ADV_DEG    = 0x0E;   // ignition timing advance, degrees before top dead centre
constexpr uint8_t PID_INTAKE_C          = 0x0F;   // intake air temperature, degrees C
constexpr uint8_t PID_MAF_GPS           = 0x10;   // mass air flow, grams per second
constexpr uint8_t PID_THROTTLE_PCT      = 0x11;   // throttle position, percent
constexpr uint8_t PID_CTRL_VOLTAGE      = 0x42;   // control module (ECU) voltage, volts; only while the ECU is awake

// Number of data bytes a PID reply carries, or 0 if the PID is not one we decode.
uint8_t elmPidDataBytes(uint8_t pid);

// Decodes the raw text the adapter sent in answer to "01<pid>". Tolerant of what real adapters do:
// spaces, lower case, echoed command, "SEARCHING...", the ">" prompt, several ECUs answering.
// On OK, `value` holds the reading in the PID's own unit (see the constants above). On any other
// status `value` is left untouched, so a failed read can never look like a real zero.
ElmStatus elmParseMode01(const char* raw, uint8_t pid, float& value);

// Same as elmParseMode01, but returns the PID's data bytes exactly as the car sent them (A, B, ...)
// instead of a converted number. `out` must hold 4 bytes; `n` is set to the byte count on OK, and
// both are left untouched on any other status. This is what goes into the log.
ElmStatus elmParseMode01Raw(const char* raw, uint8_t pid, uint8_t* out, uint8_t& n);

const char* elmStatusName(ElmStatus s);

// Builds the request text for a mode 01 PID, for example PID 0x0C gives "010C" (without the carriage
// return). `responses` (1..9) appends the digit that tells the adapter how many answers to expect, for
// example "010C1": the adapter then replies as soon as that many have arrived instead of waiting out its
// timeout, which can make each query several times faster. 0 leaves it off. Returns false if the
// buffer is too small or responses is above 9.
bool elmMode01Request(uint8_t pid, char* out, size_t cap, uint8_t responses = 0);

// ---------- Which PIDs does the car answer? ----------
// Ask PID 0x00 (covers 0x01..0x20), then 0x20 (0x21..0x40), then 0x40 (0x41..0x60). Each answer is a
// 32-bit mask; the most significant bit is the first PID of the block. Only ask the next block if the
// last bit of the current one (PID base+0x20) is set.
ElmStatus elmParseSupported(const char* raw, uint8_t basePid, uint32_t& mask);
bool elmPidSupported(uint32_t mask, uint8_t basePid, uint8_t pid);   // false if pid is outside this block

// ---------- Battery voltage measured by the adapter itself ----------
// Command "ATRV". It reads the voltage at the OBD connector, works with the engine off and does not
// need the car to answer, so it is the one to use for a parked battery-drain alarm.
extern const char* const ELM_CMD_VOLTAGE;

// Parses replies such as "12.6V", "12.6" or " 12.6 V". Values below 1.0 V are rejected as BAD_FORMAT: the
// adapter cannot be running if the battery really were that low, and some clones print 0.0V when
// they have no voltage sense. A plain "?" (an adapter without ATRV) gives UNKNOWN_COMMAND.
ElmStatus elmParseVoltage(const char* raw, float& volts);

// ---------- Collecting a reply that arrives in pieces ----------
// BLE delivers replies in small notifications (often 20 bytes). The adapter ends every reply with '>'.

constexpr size_t ELM_REPLY_MAX = 128;

struct ElmReplyBuffer {
  char text[ELM_REPLY_MAX];
  size_t len = 0;
  bool overflow = false;   // the reply was longer than the buffer; it is reported as BAD_FORMAT
  bool complete = false;   // the '>' prompt has been seen
};

// Appends a chunk. Returns true once the reply is complete (text ends at the '>', anything after it
// in the same chunk is ignored). Chunks arriving after completion are ignored until a reset.
bool elmFeed(ElmReplyBuffer& b, const char* chunk, size_t n);

// The reply collected so far, as a NUL-terminated string. If it overflowed, this is an empty string.
const char* elmReplyText(const ElmReplyBuffer& b);

void elmReplyReset(ElmReplyBuffer& b);

// ---------- Start-up sequence ----------
// ATZ reset, ATE0 echo off, ATL0 linefeeds off, ATS0 spaces off, ATH0 headers off, ATSP0 auto protocol.

constexpr uint8_t ELM_INIT_STEPS      = 6;
constexpr uint8_t ELM_INIT_MAX_FAILS  = 3;   // consecutive bad replies before giving up

struct ElmInit {
  uint8_t step = 0;     // index of the command to send next
  uint8_t fails = 0;    // consecutive bad replies to the current command
};

// The command to send now (without the trailing carriage return), or nullptr when finished or failed.
const char* elmInitCommand(const ElmInit& s);

// Feed the reply to the command just sent. Returns true if it was the expected reply and the
// sequence advanced. A wrong reply counts a failure; the same command is then sent again.
bool elmInitOnReply(ElmInit& s, const char* reply);

bool elmInitDone(const ElmInit& s);
bool elmInitFailed(const ElmInit& s);
