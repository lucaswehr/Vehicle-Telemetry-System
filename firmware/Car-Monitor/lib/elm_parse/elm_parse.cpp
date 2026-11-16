#include "elm_parse.h"
#include <string.h>

uint8_t elmPidDataBytes(uint8_t pid) {
  switch (pid) {
    case PID_RPM:
    case PID_MAF_GPS:
    case PID_CTRL_VOLTAGE:        return 2;
    case PID_ENGINE_LOAD_PCT:
    case PID_COOLANT_C:
    case PID_FUEL_TRIM_ST_PCT:
    case PID_FUEL_TRIM_LT_PCT:
    case PID_MAP_KPA:
    case PID_SPEED_KMH:
    case PID_TIMING_ADV_DEG:
    case PID_INTAKE_C:
    case PID_THROTTLE_PCT:        return 1;
    default:                      return 0;
  }
}

const char* elmStatusName(ElmStatus s) {
  switch (s) {
    case ElmStatus::OK:                return "OK";
    case ElmStatus::NO_DATA:           return "NO DATA";
    case ElmStatus::UNABLE_TO_CONNECT: return "UNABLE TO CONNECT";
    case ElmStatus::BUS_ERROR:         return "BUS ERROR";
    case ElmStatus::UNKNOWN_COMMAND:   return "UNKNOWN COMMAND";
    case ElmStatus::NEGATIVE_RESPONSE: return "NEGATIVE RESPONSE";
    case ElmStatus::WRONG_PID:         return "WRONG PID";
    case ElmStatus::UNSUPPORTED_PID:   return "UNSUPPORTED PID";
    case ElmStatus::BAD_FORMAT:        return "BAD FORMAT";
  }
  return "?";
}

static int hexVal(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  return -1;
}

static bool startsWith(const char* s, const char* p) { return strncmp(s, p, strlen(p)) == 0; }

static ElmStatus classifyError(const char* c) {
  if (strcmp(c, "NODATA") == 0)                return ElmStatus::NO_DATA;
  if (startsWith(c, "UNABLETOCONNECT"))        return ElmStatus::UNABLE_TO_CONNECT;
  if (strcmp(c, "?") == 0)                     return ElmStatus::UNKNOWN_COMMAND;
  if (startsWith(c, "BUSINIT") || startsWith(c, "BUSBUSY") || startsWith(c, "BUFFERFULL") ||
      startsWith(c, "CANERROR") || startsWith(c, "FBERROR") || startsWith(c, "DATAERROR") ||
      startsWith(c, "LVRESET") || startsWith(c, "STOPPED") || strcmp(c, "ERROR") == 0 ||
      strstr(c, "ERROR") != nullptr)
    return ElmStatus::BUS_ERROR;
  return ElmStatus::BAD_FORMAT;
}

static float decode(uint8_t pid, const uint8_t* d) {
  switch (pid) {
    case PID_RPM:             return (float)((d[0] << 8) | d[1]) / 4.0f;
    case PID_SPEED_KMH:       return (float)d[0];
    case PID_COOLANT_C:       return (float)d[0] - 40.0f;
    case PID_INTAKE_C:        return (float)d[0] - 40.0f;
    case PID_THROTTLE_PCT:    return (float)d[0] * 100.0f / 255.0f;
    case PID_ENGINE_LOAD_PCT: return (float)d[0] * 100.0f / 255.0f;
    case PID_FUEL_TRIM_ST_PCT:
    case PID_FUEL_TRIM_LT_PCT: return ((float)d[0] - 128.0f) * 100.0f / 128.0f;
    case PID_MAP_KPA:         return (float)d[0];
    case PID_TIMING_ADV_DEG:  return (float)d[0] / 2.0f - 64.0f;
    case PID_MAF_GPS:         return (float)((d[0] << 8) | d[1]) / 100.0f;
    case PID_CTRL_VOLTAGE:    return (float)((d[0] << 8) | d[1]) / 1000.0f;
  }
  return 0.0f;
}

// Finds the answer to "01<pid>" in the reply and copies its `need` data bytes to `out`.
static ElmStatus extractData(const char* raw, uint8_t pid, uint8_t need, uint8_t* out) {
  if (!raw) return ElmStatus::BAD_FORMAT;

  char echo[5] = {'0', '1', "0123456789ABCDEF"[pid >> 4], "0123456789ABCDEF"[pid & 15], 0};
  ElmStatus problem = ElmStatus::BAD_FORMAT;   // the most informative failure seen so far

  const char* p = raw;
  while (*p) {
    // Take one line (ended by CR or LF) and compact it: upper case, no spaces, no '>' prompt.
    char c[96];
    size_t n = 0;
    bool tooLong = false;
    while (*p && *p != '\r' && *p != '\n') {
      char ch = *p++;
      if (ch == ' ' || ch == '\t' || ch == '>') continue;
      if (ch >= 'a' && ch <= 'z') ch = (char)(ch - 'a' + 'A');
      if (n + 1 < sizeof c) c[n++] = ch; else tooLong = true;
    }
    while (*p == '\r' || *p == '\n') p++;
    c[n] = '\0';
    if (n == 0 || tooLong) continue;

    const char* line = c;
    if (startsWith(line, "SEARCHING...")) line += 12;      // may be glued to the next text
    if (*line == '\0' || strcmp(line, echo) == 0) continue;  // blank, or our own command echoed back

    // A data line is an even number of hex digits.
    size_t len = strlen(line);
    bool hex = (len % 2 == 0);
    for (size_t i = 0; hex && i < len; i++) if (hexVal(line[i]) < 0) hex = false;

    if (!hex) {
      ElmStatus e = classifyError(line);
      if (e != ElmStatus::BAD_FORMAT && problem == ElmStatus::BAD_FORMAT) problem = e;
      continue;
    }

    uint8_t b[16];
    size_t nb = len / 2;
    if (nb > sizeof b) continue;
    for (size_t i = 0; i < nb; i++) b[i] = (uint8_t)(hexVal(line[2 * i]) * 16 + hexVal(line[2 * i + 1]));

    if (b[0] == 0x7F) { if (problem == ElmStatus::BAD_FORMAT) problem = ElmStatus::NEGATIVE_RESPONSE; continue; }
    if (b[0] != 0x41 || nb < 2) continue;                    // not a mode 01 answer
    if (b[1] != pid) { problem = ElmStatus::WRONG_PID; continue; }
    if (nb - 2 < need) continue;                              // truncated: keep looking, else BAD_FORMAT
    for (uint8_t i = 0; i < need; i++) out[i] = b[2 + i];
    return ElmStatus::OK;
  }
  return problem;
}

ElmStatus elmParseMode01(const char* raw, uint8_t pid, float& value) {
  uint8_t need = elmPidDataBytes(pid);
  if (need == 0) return ElmStatus::UNSUPPORTED_PID;
  uint8_t d[4];
  ElmStatus s = extractData(raw, pid, need, d);
  if (s == ElmStatus::OK) value = decode(pid, d);
  return s;
}

bool elmMode01Request(uint8_t pid, char* out, size_t cap) {
  if (!out || cap < 5) return false;
  const char* hex = "0123456789ABCDEF";
  out[0] = '0'; out[1] = '1'; out[2] = hex[pid >> 4]; out[3] = hex[pid & 15]; out[4] = '\0';
  return true;
}

ElmStatus elmParseSupported(const char* raw, uint8_t basePid, uint32_t& mask) {
  if (basePid != 0x00 && basePid != 0x20 && basePid != 0x40 && basePid != 0x60 && basePid != 0x80 &&
      basePid != 0xA0 && basePid != 0xC0)
    return ElmStatus::UNSUPPORTED_PID;
  uint8_t d[4];
  ElmStatus s = extractData(raw, basePid, 4, d);
  if (s == ElmStatus::OK) mask = ((uint32_t)d[0] << 24) | ((uint32_t)d[1] << 16) | ((uint32_t)d[2] << 8) | d[3];
  return s;
}

bool elmPidSupported(uint32_t mask, uint8_t basePid, uint8_t pid) {
  if (pid <= basePid || pid > (uint8_t)(basePid + 0x20) || (unsigned)basePid + 0x20 > 0xFF) return false;
  return (mask >> (31 - (pid - basePid - 1))) & 1u;
}

// ---------- battery voltage ----------

const char* const ELM_CMD_VOLTAGE = "ATRV";

ElmStatus elmParseVoltage(const char* raw, float& volts) {
  if (!raw) return ElmStatus::BAD_FORMAT;
  ElmStatus problem = ElmStatus::BAD_FORMAT;
  const char* p = raw;
  while (*p) {
    char c[40];
    size_t n = 0;
    while (*p && *p != '\r' && *p != '\n') {
      char ch = *p++;
      if (ch == ' ' || ch == '\t' || ch == '>') continue;
      if (ch >= 'a' && ch <= 'z') ch = (char)(ch - 'a' + 'A');
      if (n + 1 < sizeof c) c[n++] = ch; else n = sizeof c;   // too long: mark and skip below
    }
    while (*p == '\r' || *p == '\n') p++;
    if (n == 0 || n >= sizeof c) continue;
    c[n] = '\0';
    if (strcmp(c, "ATRV") == 0) continue;                      // echoed command

    // digits, optional '.', digits, optional trailing 'V'
    size_t i = 0;
    float whole = 0, frac = 0, scale = 0.1f;
    int digits = 0;
    while (i < n && c[i] >= '0' && c[i] <= '9') { whole = whole * 10 + (c[i] - '0'); i++; digits++; }
    if (i < n && c[i] == '.') {
      i++;
      while (i < n && c[i] >= '0' && c[i] <= '9') { frac += (c[i] - '0') * scale; scale *= 0.1f; i++; digits++; }
    }
    if (i < n && c[i] == 'V') i++;
    if (digits > 0 && i == n) {
      float v = whole + frac;
      if (v < 1.0f || v > 40.0f) continue;                     // not a plausible battery voltage
      volts = v;
      return ElmStatus::OK;
    }
    ElmStatus e = classifyError(c);
    if (e != ElmStatus::BAD_FORMAT && problem == ElmStatus::BAD_FORMAT) problem = e;
  }
  return problem;
}

// ---------- reply assembly ----------

bool elmFeed(ElmReplyBuffer& b, const char* chunk, size_t n) {
  if (b.complete || !chunk) return b.complete;
  for (size_t i = 0; i < n; i++) {
    char ch = chunk[i];
    if (ch == '>') { b.complete = true; break; }
    if (b.len + 1 < ELM_REPLY_MAX) b.text[b.len++] = ch;
    else b.overflow = true;
  }
  return b.complete;
}

const char* elmReplyText(const ElmReplyBuffer& b) {
  static const char empty[] = "";
  if (b.overflow) return empty;
  // text[] is not NUL-terminated while filling; terminate on a copy-free basis by using the spare byte.
  ElmReplyBuffer& m = const_cast<ElmReplyBuffer&>(b);
  m.text[m.len] = '\0';
  return b.text;
}

void elmReplyReset(ElmReplyBuffer& b) { b.len = 0; b.overflow = false; b.complete = false; }

// ---------- start-up sequence ----------

static const char* const INIT_CMDS[ELM_INIT_STEPS] = {"ATZ", "ATE0", "ATL0", "ATS0", "ATH0", "ATSP0"};

bool elmInitDone(const ElmInit& s)   { return s.step >= ELM_INIT_STEPS; }
bool elmInitFailed(const ElmInit& s) { return s.fails >= ELM_INIT_MAX_FAILS; }

const char* elmInitCommand(const ElmInit& s) {
  if (elmInitDone(s) || elmInitFailed(s)) return nullptr;
  return INIT_CMDS[s.step];
}

bool elmInitOnReply(ElmInit& s, const char* reply) {
  if (elmInitDone(s) || elmInitFailed(s) || !reply) return false;
  // ATZ answers with the adapter's version string ("ELM327 v1.5"); everything else answers "OK".
  // Echo may still be on for the first commands, so the command itself can appear before the answer.
  bool ok = (s.step == 0) ? (strstr(reply, "ELM327") != nullptr) : (strstr(reply, "OK") != nullptr);
  if (ok) { s.step++; s.fails = 0; return true; }
  s.fails++;
  return false;
}
