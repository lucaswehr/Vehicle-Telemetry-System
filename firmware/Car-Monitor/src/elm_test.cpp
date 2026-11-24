// ELM327 bring-up test (Bluetooth Classic). Not part of the logger firmware.
//   pio run -e elm_test -t upload && pio device monitor
//
// Plug the adapter into the car's OBD-II port and turn the key to ACC or ON (engine running is best).
// The sketch scans for Bluetooth Classic devices, connects to the adapter, runs the start-up commands,
// asks which protocol the car uses, reads the battery voltage, asks which PIDs the car supports and then
// polls them forever, printing every value and how long each query took.
//
// Copy everything it prints from "=== ELM TEST" to "=== SUMMARY" back into the chat.

#include <Arduino.h>
#include <BluetoothSerial.h>
#include <string.h>
#include "elm_parse.h"

// ----- settings you may want to change -----
static const char*    PIN_FIRST   = "1234";    // most adapters; "0000" is tried second
static const char*    PIN_SECOND  = "0000";
static const uint32_t SCAN_MS     = 10000;
static const uint32_t REPLY_MS    = 3000;      // normal command timeout
static const uint32_t FIRST_PID_MS = 12000;    // the first PID request can take a long time while the adapter searches protocols
// -------------------------------------------

BluetoothSerial SerialBT;

struct PidInfo { uint8_t pid; const char* name; const char* unit; };
static const PidInfo PIDS[] = {
  {PID_RPM,             "engine speed",     "rpm"},
  {PID_SPEED_KMH,       "vehicle speed",    "km/h"},
  {PID_COOLANT_C,       "coolant temp",     "C"},
  {PID_THROTTLE_PCT,    "throttle",         "%"},
  {PID_ENGINE_LOAD_PCT, "engine load",      "%"},
  {PID_MAP_KPA,         "manifold pressure","kPa"},
  {PID_INTAKE_C,        "intake air temp",  "C"},
  {PID_MAF_GPS,         "mass air flow",    "g/s"},
  {PID_TIMING_ADV_DEG,  "timing advance",   "deg"},
  {PID_FUEL_TRIM_ST_PCT,"fuel trim short",  "%"},
  {PID_FUEL_TRIM_LT_PCT,"fuel trim long",   "%"},
  {PID_CTRL_VOLTAGE,    "ECU voltage",      "V"},
};
static const size_t N_PIDS = sizeof PIDS / sizeof PIDS[0];

static uint32_t masks[3];            // supported-PID masks for blocks 0x00, 0x20, 0x40
static bool     haveMask[3] = {false, false, false};
static bool     firstQueryDone = false;
static uint8_t  responseDigit = 0;   // 1 = append the "expect one answer" digit to every request (set by the benchmark)
static bool     btUp = false;        // Bluetooth stack started (SerialBT.end() is only safe after begin())
static bool     showRaw = true;      // print every raw reply (with \r shown); switched off while polling

// ---------- helpers ----------

static void printEscaped(const char* s) {
  Serial.print('"');
  for (; *s; s++) {
    if (*s == '\r') Serial.print("\\r");
    else if (*s == '\n') Serial.print("\\n");
    else Serial.print(*s);
  }
  Serial.print('"');
}

static void drainInput() {
  while (SerialBT.available()) SerialBT.read();
}

// Sends one command and waits for the '>' prompt. Returns true if a complete reply arrived.
static bool sendAndWait(const char* cmd, ElmReplyBuffer& b, uint32_t timeoutMs, uint32_t* tookMs = nullptr) {
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
    if (!b.complete) delay(1);
  }
  uint32_t took = millis() - t0;
  if (tookMs) *tookMs = took;
  if (showRaw) {
    Serial.printf("  > %s   <- ", cmd);
    printEscaped(elmReplyText(b));
    Serial.printf("%s  (%lu ms)\n", b.complete ? "" : "  [TIMEOUT, no '>' prompt]", (unsigned long)took);
  }
  return b.complete;
}

static bool startBluetooth(const char* pin) {
  if (btUp) { SerialBT.end(); delay(300); btUp = false; }
#if ESP_ARDUINO_VERSION_MAJOR >= 3
  SerialBT.setPin(pin, (uint8_t)strlen(pin));
#else
  SerialBT.setPin(pin);
#endif
  btUp = SerialBT.begin("ESP32-OBD-TEST", true);   // true = we are the master, the adapter is the slave
  return btUp;
}

static bool looksLikeAdapter(const char* name) {
  String n(name);
  n.toUpperCase();
  return n.indexOf("OBD") >= 0 || n.indexOf("ELM") >= 0 || n.indexOf("V-LINK") >= 0 ||
         n.indexOf("VLINK") >= 0 || n.indexOf("VGATE") >= 0 || n.indexOf("IOS-VLINK") >= 0;
}

// Scans and lets the user (or the name) pick a device. Returns false if nothing usable was found.
static bool scanAndConnect() {
  const char* pins[2] = {PIN_FIRST, PIN_SECOND};
  for (int p = 0; p < 2; p++) {
    Serial.printf("Starting Bluetooth Classic (PIN %s)...\n", pins[p]);
    if (!startBluetooth(pins[p])) { Serial.println("  Bluetooth failed to start"); return false; }

    Serial.printf("Scanning for %lu s. The adapter must be powered (key in ACC/ON)...\n", (unsigned long)(SCAN_MS / 1000));
    BTScanResults* res = SerialBT.discover(SCAN_MS);
    int n = res ? res->getCount() : 0;
    Serial.printf("Found %d Classic device(s):\n", n);
    int pick = -1;
    for (int i = 0; i < n; i++) {
      BTAdvertisedDevice* d = res->getDevice(i);
      std::string name = d->getName();
      Serial.printf("  [%d] name=\"%s\"  address=%s  rssi=%d\n", i, name.c_str(), d->getAddress().toString().c_str(), d->getRSSI());
      if (pick < 0 && looksLikeAdapter(name.c_str())) pick = i;
    }
    if (n == 0) { Serial.println("  none. (If the adapter is BLE-only it will not appear here.)"); return false; }

    if (pick < 0) {
      Serial.println("No name looks like an OBD adapter. Type the number of the device to use and press Enter (20 s)...");
      uint32_t t0 = millis();
      while (millis() - t0 < 20000 && pick < 0) {
        if (Serial.available()) {
          int v = Serial.parseInt();
          if (v >= 0 && v < n) pick = v;
        }
        delay(10);
      }
      if (pick < 0) { Serial.println("No choice made."); return false; }
    }

    BTAdvertisedDevice* d = res->getDevice(pick);
    Serial.printf("Connecting to \"%s\" %s ...\n", d->getName().c_str(), d->getAddress().toString().c_str());
    bool ok = SerialBT.connect(d->getAddress());
    if (ok && SerialBT.connected(3000)) { Serial.printf("CONNECTED with PIN %s\n", pins[p]); return true; }
    Serial.println("  connect failed" );
  }
  return false;
}

// ---------- the test steps ----------

static bool runInit() {
  ElmInit s;
  ElmReplyBuffer b;
  Serial.println("--- start-up sequence");
  while (!elmInitDone(s) && !elmInitFailed(s)) {
    const char* cmd = elmInitCommand(s);
    uint32_t took = 0;
    bool got = sendAndWait(cmd, b, strcmp(cmd, "ATZ") == 0 ? 5000 : REPLY_MS, &took);
    if (!got) { s.fails++; continue; }
    elmInitOnReply(s, elmReplyText(b));
  }
  Serial.printf("start-up: %s\n", elmInitDone(s) ? "OK" : "FAILED");
  return elmInitDone(s);
}

static void readVoltage() {
  ElmReplyBuffer b;
  uint32_t took = 0;
  if (!sendAndWait(ELM_CMD_VOLTAGE, b, REPLY_MS, &took)) { Serial.println("voltage: no reply"); return; }
  float v = 0;
  ElmStatus st = elmParseVoltage(elmReplyText(b), v);
  if (st == ElmStatus::OK) Serial.printf("RESULT adapter voltage (ATRV): %.2f V   (compare with a multimeter)\n", v);
  else Serial.printf("RESULT adapter voltage (ATRV): %s\n", elmStatusName(st));
}

static void showProtocol() {
  ElmReplyBuffer b;
  uint32_t took = 0;
  Serial.println("--- adapter version and the protocol it found (look at the replies below)");
  sendAndWait("ATI", b, REPLY_MS, &took);                    // adapter version
  sendAndWait("ATDP", b, REPLY_MS, &took);                   // describe protocol, e.g. "SAE J1850 PWM" or "ISO 15765-4 (CAN 11/500)"
}

static bool readMask(uint8_t base, int idx) {
  char req[8];
  elmMode01Request(base, req, sizeof req);
  ElmReplyBuffer b;
  uint32_t took = 0;
  uint32_t timeout = firstQueryDone ? REPLY_MS : FIRST_PID_MS;
  bool got = sendAndWait(req, b, timeout, &took);
  firstQueryDone = true;
  if (!got) return false;
  ElmStatus st = elmParseSupported(elmReplyText(b), base, masks[idx]);
  if (st != ElmStatus::OK) { Serial.printf("supported PIDs %s: %s\n", req, elmStatusName(st)); return false; }
  haveMask[idx] = true;
  Serial.printf("supported PIDs %s: mask %08lX\n", req, (unsigned long)masks[idx]);
  return true;
}

static bool pidIsSupported(uint8_t pid) {
  int idx = pid / 0x20;
  if (idx > 2 || !haveMask[idx]) return false;
  return elmPidSupported(masks[idx], (uint8_t)(idx * 0x20), pid);
}

static void discoverSupported() {
  Serial.println("--- asking the car which PIDs it supports");
  if (!readMask(0x00, 0)) { Serial.println("The car did not answer 0100. Is the ignition on? Is the engine running?"); return; }
  if (elmPidSupported(masks[0], 0x00, 0x20)) readMask(0x20, 1);
  if (haveMask[1] && elmPidSupported(masks[1], 0x20, 0x40)) readMask(0x40, 2);
  Serial.print("RESULT PIDs the car supports that this test knows how to read:");
  bool any = false;
  for (size_t i = 0; i < N_PIDS; i++) if (pidIsSupported(PIDS[i].pid)) { Serial.printf(" %02X(%s)", PIDS[i].pid, PIDS[i].name); any = true; }
  Serial.println(any ? "" : " none");
}

// ---------- speed benchmark ----------
// Tries the adapter's timing options and measures how long a query takes with each, then keeps the fastest
// one that gave no failures. Each variant sends 40 requests (RPM, throttle, load, speed in turn).

struct Variant { const char* name; uint8_t responses; const char* at1; const char* at2; };
static const Variant VARIANTS[] = {
  {"A default timing, no response count",              0, nullptr, nullptr},
  {"B response count 1",                               1, nullptr, nullptr},
  {"C response count 1 + ATAT2",                       1, "ATAT2", nullptr},
  {"D response count 1 + ATAT2 + ATST19 (100 ms wait)", 1, "ATAT2", "ATST19"},
};
static const size_t N_VARIANTS = sizeof VARIANTS / sizeof VARIANTS[0];
static const uint8_t BENCH_PIDS[] = {PID_RPM, PID_THROTTLE_PCT, PID_ENGINE_LOAD_PCT, PID_SPEED_KMH};
static const int BENCH_QUERIES = 40;

struct BenchResult { uint32_t ok = 0, fail = 0, totalMs = 0, maxMs = 0; };

static void sendQuiet(const char* cmd) {
  ElmReplyBuffer b;
  uint32_t took = 0;
  bool saved = showRaw;
  showRaw = false;
  sendAndWait(cmd, b, REPLY_MS, &took);
  showRaw = saved;
}

static BenchResult runVariant(const Variant& v) {
  BenchResult r;
  sendQuiet("ATAT1");                  // back to the adapter's defaults first
  sendQuiet("ATST32");
  if (v.at1) sendQuiet(v.at1);
  if (v.at2) sendQuiet(v.at2);
  for (int i = 0; i < BENCH_QUERIES; i++) {
    uint8_t pid = BENCH_PIDS[i % 4];
    char req[8];
    elmMode01Request(pid, req, sizeof req, v.responses);
    ElmReplyBuffer b;
    uint32_t took = 0;
    showRaw = (i == 0);                // show one raw reply per variant, as a sanity check
    bool got = sendAndWait(req, b, REPLY_MS, &took);
    showRaw = false;
    float val = 0;
    ElmStatus st = got ? elmParseMode01(elmReplyText(b), pid, val) : ElmStatus::BAD_FORMAT;
    if (st == ElmStatus::OK) { r.ok++; r.totalMs += took; if (took > r.maxMs) r.maxMs = took; }
    else r.fail++;
  }
  return r;
}

static void runBenchmark() {
  Serial.println("--- speed benchmark (about 30 s)");
  int best = 0;
  uint32_t bestAvg = 0xFFFFFFFFu;
  for (size_t i = 0; i < N_VARIANTS; i++) {
    BenchResult r = runVariant(VARIANTS[i]);
    uint32_t avg = r.ok ? r.totalMs / r.ok : 0;
    Serial.printf("RESULT %s: %lu ok, %lu failed, average %lu ms, slowest %lu ms, %.1f queries/s\n",
                  VARIANTS[i].name, (unsigned long)r.ok, (unsigned long)r.fail, (unsigned long)avg,
                  (unsigned long)r.maxMs, avg ? 1000.0f / avg : 0.0f);
    if (r.fail == 0 && r.ok > 0 && avg < bestAvg) { bestAvg = avg; best = (int)i; }
  }
  Serial.printf("RESULT fastest variant with no failures: %s\n", VARIANTS[best].name);
  sendQuiet("ATAT1");
  sendQuiet("ATST32");
  if (VARIANTS[best].at1) sendQuiet(VARIANTS[best].at1);
  if (VARIANTS[best].at2) sendQuiet(VARIANTS[best].at2);
  responseDigit = VARIANTS[best].responses;
}

static void pollForever() {
  Serial.println("--- polling (values and query times). Rev the engine / drive to see them change.");
  showRaw = false;
  uint32_t cycle = 0, queries = 0, totalMs = 0, maxMs = 0, failures = 0;
  uint32_t startMs = millis();
  for (;;) {
    bool any = false;
    for (size_t i = 0; i < N_PIDS; i++) {
      // Poll what the car says it supports; if the support check failed, try everything.
      if ((haveMask[0] && !pidIsSupported(PIDS[i].pid))) continue;
      any = true;
      char req[8];
      elmMode01Request(PIDS[i].pid, req, sizeof req, responseDigit);
      ElmReplyBuffer b;
      uint32_t took = 0;
      bool got = sendAndWait(req, b, REPLY_MS, &took);
      queries++; totalMs += took; if (took > maxMs) maxMs = took;
      float v = 0;
      ElmStatus st = got ? elmParseMode01(elmReplyText(b), PIDS[i].pid, v) : ElmStatus::BAD_FORMAT;
      if (st == ElmStatus::OK) Serial.printf("%-18s %9.2f %-4s (%lu ms)\n", PIDS[i].name, v, PIDS[i].unit, (unsigned long)took);
      else { failures++; Serial.printf("%-18s %s%s\n", PIDS[i].name, elmStatusName(st), got ? "" : " (timeout)"); }
      if (!SerialBT.connected()) { Serial.println("Bluetooth connection lost. Restarting..."); delay(1000); ESP.restart(); }
    }
    cycle++;
    if (cycle % 5 == 0) {
      readVoltage();
      Serial.printf("RESULT after %lu cycles: %lu queries, average %lu ms, slowest %lu ms, %lu failures, %.1f queries/s\n",
                    (unsigned long)cycle, (unsigned long)queries, (unsigned long)(queries ? totalMs / queries : 0),
                    (unsigned long)maxMs, (unsigned long)failures, queries * 1000.0f / (float)(millis() - startMs));
    }
    if (!any) { Serial.println("Nothing to poll. Check the supported-PID results above."); delay(3000); }
  }
}

void setup() {
  Serial.begin(115200);
  delay(1500);
  Serial.println();
  Serial.println("=== ELM TEST (Bluetooth Classic) ===");

  if (!scanAndConnect()) {
    Serial.println("=== SUMMARY: could not connect over Bluetooth Classic. ===");
    Serial.println("Next steps: check the adapter's LED is lit (key in ACC/ON), try pairing it once from a phone, or it may be BLE-only.");
    return;
  }

  if (!runInit()) {
    Serial.println("=== SUMMARY: connected, but the adapter did not complete start-up. See the raw replies above. ===");
    return;
  }
  readVoltage();                 // works even if the car does not answer
  discoverSupported();           // the first query can take several seconds while the adapter finds the protocol
  showProtocol();                // ask after the first query, so the protocol has been detected
  runBenchmark();                // find the fastest timing settings for this adapter and car
  Serial.println("=== SUMMARY: copy everything from '=== ELM TEST' down to the RESULT lines above and send it back. ===");
  pollForever();
}

void loop() {
  delay(1000);
}
