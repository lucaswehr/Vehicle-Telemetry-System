#include <Arduino.h>
#include <BluetoothSerial.h>
#include <string.h>
#include "elm327.h"
#include "elm_parse.h"
#include "elm_poll.h"

// ----- settings -----
static const uint8_t  ADAPTER_ADDR[6] = {0x00, 0x10, 0xCC, 0x4F, 0x36, 0x03};   // your adapter ("OBDII")
static const char*    ADAPTER_NAME    = "OBDII";
static const char*    ADAPTER_PIN     = "1234";
static const uint32_t REPLY_MS        = 1500;     // normal reply timeout (queries take about 65 ms)
static const uint32_t SEARCH_REPLY_MS = 12000;    // after a silent car the adapter may be searching for the protocol
static const uint32_t INIT_REPLY_MS   = 3000;
static const uint8_t  RESPONSES_HINT  = 1;        // "010C1": reply as soon as one answer arrives (4x faster)
// ---------------------

static BluetoothSerial SerialBT;
static bool btUp = false;
static ElmPoll poll;

static ElmSnapshot snap;
static portMUX_TYPE snapMux = portMUX_INITIALIZER_UNLOCKED;
static QueueHandle_t logQueue = nullptr;
static volatile uint32_t queueDrops = 0;

static void setLink(ElmLink l) {
  portENTER_CRITICAL(&snapMux);
  snap.link = l;
  portEXIT_CRITICAL(&snapMux);
}

static void setSample(Sample ElmSnapshot::*field, float v, uint32_t now) {
  portENTER_CRITICAL(&snapMux);
  sampleSet(snap.*field, v, now);
  portEXIT_CRITICAL(&snapMux);
}

void elmGetSnapshot(ElmSnapshot& out) {
  portENTER_CRITICAL(&snapMux);
  out = snap;
  portEXIT_CRITICAL(&snapMux);
}

bool elmPopLog(ElmLogItem& out) {
  return logQueue && xQueueReceive(logQueue, &out, 0) == pdTRUE;
}

uint32_t elmQueueDrops() { return queueDrops; }

// ---------- talking to the adapter ----------

static void drainInput() {
  while (SerialBT.available()) SerialBT.read();
}

// Sends one command and waits for the '>' prompt. True if a complete reply arrived in time.
static bool sendAndWait(const char* cmd, ElmReplyBuffer& b, uint32_t timeoutMs) {
  drainInput();
  elmReplyReset(b);
  uint32_t t0 = millis();
  SerialBT.print(cmd);
  SerialBT.print('\r');
  while (!b.complete && millis() - t0 < timeoutMs) {
    while (!b.complete && SerialBT.available()) {
      char c = (char)SerialBT.read();
      elmFeed(b, &c, 1);
    }
    if (!b.complete) vTaskDelay(1);
  }
  return b.complete;
}

static bool startBluetooth() {
  if (btUp) return true;
#if ESP_ARDUINO_VERSION_MAJOR >= 3
  SerialBT.setPin(ADAPTER_PIN, (uint8_t)strlen(ADAPTER_PIN));
#else
  SerialBT.setPin(ADAPTER_PIN);
#endif
  btUp = SerialBT.begin("ESP32-OBD", true);      // true = we are the master, the adapter is the slave
  return btUp;
}

static bool runInit() {
  ElmInit s;
  ElmReplyBuffer b;
  while (!elmInitDone(s) && !elmInitFailed(s)) {
    const char* cmd = elmInitCommand(s);
    bool got = sendAndWait(cmd, b, strcmp(cmd, "ATZ") == 0 ? 5000 : INIT_REPLY_MS);
    if (!got) { s.fails++; continue; }
    elmInitOnReply(s, elmReplyText(b));
  }
  return elmInitDone(s);
}

// Connects (by address first, then by name) and runs the start-up commands. Blocks for seconds; that is
// why this whole file runs in its own task.
static bool connectAndInit() {
  if (!startBluetooth()) return false;
  if (SerialBT.connected()) SerialBT.disconnect();
  uint8_t addr[6];
  memcpy(addr, ADAPTER_ADDR, sizeof addr);
  bool ok = SerialBT.connect(addr) && SerialBT.connected(3000);
  if (!ok) ok = SerialBT.connect(String(ADAPTER_NAME)) && SerialBT.connected(3000);
  if (!ok) return false;
  if (!runInit()) { SerialBT.disconnect(); return false; }
  return true;
}

// ---------- one request ----------

struct Result {
  ElmStatus status;
  uint8_t   raw[4];
  uint8_t   n;
  float     value;
};

static Result doRequest(const PollRequest& req, bool carAnsweredLast) {
  Result r = {ElmStatus::BAD_FORMAT, {0, 0, 0, 0}, 0, 0.0f};
  ElmReplyBuffer b;
  if (req.kind == PollKind::VOLTAGE) {
    bool got = sendAndWait(ELM_CMD_VOLTAGE, b, REPLY_MS);
    float v = 0;
    r.status = got ? elmParseVoltage(elmReplyText(b), v) : ElmStatus::BAD_FORMAT;
    if (r.status == ElmStatus::OK) {
      uint16_t mv = pollVoltageMv(v);
      r.raw[0] = (uint8_t)(mv >> 8);
      r.raw[1] = (uint8_t)(mv & 0xFF);
      r.n = 2;
      r.value = v;
    }
    return r;
  }
  char cmd[8];
  if (!elmMode01Request(req.pid, cmd, sizeof cmd, RESPONSES_HINT)) return r;
  bool got = sendAndWait(cmd, b, carAnsweredLast ? REPLY_MS : SEARCH_REPLY_MS);
  if (!got) return r;
  r.status = elmParseMode01Raw(elmReplyText(b), req.pid, r.raw, r.n);
  if (r.status == ElmStatus::OK) {
    float v = 0;
    if (elmParseMode01(elmReplyText(b), req.pid, v) == ElmStatus::OK) r.value = v;
  }
  return r;
}

static void storeValue(uint8_t pid, float v, uint32_t now) {
  switch (pid) {
    case PID_RPM:               setSample(&ElmSnapshot::rpm, v, now); break;
    case PID_SPEED_KMH:         setSample(&ElmSnapshot::speedKmh, v, now); break;
    case PID_THROTTLE_PCT:      setSample(&ElmSnapshot::throttlePct, v, now); break;
    case PID_ENGINE_LOAD_PCT:   setSample(&ElmSnapshot::loadPct, v, now); break;
    case PID_COOLANT_C:         setSample(&ElmSnapshot::coolantC, v, now); break;
    case POLL_PID_VOLTAGE:      setSample(&ElmSnapshot::volts, v, now); break;
    default: break;             // other PIDs are only logged
  }
}

static ElmLink linkFor(PollMode m) {
  switch (m) {
    case PollMode::NORMAL:  return ElmLink::LIVE;
    case PollMode::KEY_OFF: return ElmLink::KEY_OFF;
    default:                return ElmLink::DOWN;
  }
}

// ---------- the task ----------

static void elmTask(void*) {
  pollInit(poll, 0);
  bool everConnected = false;
  bool carAnsweredLast = false;
  PollMode lastMode = PollMode::DOWN;

  pollOnLinkLost(poll, millis());
  poll.retryAtMs = millis();                       // first connection attempt right away
  setLink(ElmLink::CONNECTING);

  for (;;) {
    uint32_t now = millis();

    if (poll.mode == PollMode::DOWN) {
      if (pollReconnectDue(poll, now)) {
        setLink(ElmLink::CONNECTING);
        Serial.println("ELM: connecting...");
        bool ok = connectAndInit();
        pollOnReconnect(poll, ok, millis());
        if (ok) {
          everConnected = true;
          carAnsweredLast = false;                 // the first PID request may wait for the protocol search
          lastMode = PollMode::NORMAL;
          setLink(ElmLink::LIVE);
          Serial.println("ELM: connected");
        } else {
          setLink(everConnected ? ElmLink::DOWN : ElmLink::OFF);
          Serial.printf("ELM: connect failed, retry in %lu ms\n", (unsigned long)poll.backoffMs);
        }
      } else {
        vTaskDelay(pdMS_TO_TICKS(100));
      }
      continue;
    }

    if (!SerialBT.connected()) {                   // the adapter dropped the link
      pollOnLinkLost(poll, now);
      setLink(ElmLink::DOWN);
      Serial.println("ELM: link lost");
      continue;
    }

    PollRequest req;
    if (!pollNext(poll, now, req)) { vTaskDelay(pdMS_TO_TICKS(5)); continue; }

    Result r = doRequest(req, carAnsweredLast);
    uint32_t done = millis();
    if (req.kind == PollKind::PID) carAnsweredLast = (r.status == ElmStatus::OK);

    PollLog log;
    pollOnResult(poll, r.status, done, log);
    if (r.status == ElmStatus::OK) storeValue(req.pid, r.value, done);

    if (log.log && logQueue) {
      ElmLogItem it;
      it.tMs = done;
      it.pid = log.pid;
      it.status = log.status;
      it.nbytes = (log.status == POLL_LOG_OK) ? r.n : 0;
      memset(it.raw, 0, sizeof it.raw);
      if (it.nbytes) memcpy(it.raw, r.raw, it.nbytes);
      if (xQueueSend(logQueue, &it, 0) != pdTRUE) queueDrops++;
    }

    if (poll.mode != lastMode) {
      lastMode = poll.mode;
      if (poll.mode == PollMode::KEY_OFF) Serial.println("ELM: car stopped answering (key off)");
      else if (poll.mode == PollMode::NORMAL) Serial.println("ELM: car answering again");
      if (poll.mode != PollMode::DOWN) setLink(linkFor(poll.mode));
    }
  }
}

void elmStart() {
  if (logQueue) return;
  logQueue = xQueueCreate(64, sizeof(ElmLogItem));
  xTaskCreatePinnedToCore(elmTask, "elm", 8192, nullptr, 1, nullptr, 0);   // core 0, next to the Bluetooth stack
}
