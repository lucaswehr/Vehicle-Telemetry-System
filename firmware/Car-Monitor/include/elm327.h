#pragma once
#include <stdint.h>
#include "dashmodel.h"      // Sample, ElmLink

// Hardware layer for the ELM327 Bluetooth Classic adapter. It runs in its own FreeRTOS task, so the
// slow parts (finding the adapter, connecting, waiting for replies) never stall GPS, SD logging or the
// screen. The task asks the car what lib/elm_poll tells it to; this file only moves text in and out.

// The latest readings, each with the time it arrived (millis). Copy it out with elmGetSnapshot().
struct ElmSnapshot {
  Sample  rpm, speedKmh, throttlePct, loadPct, coolantC, volts;
  ElmLink link = ElmLink::OFF;
};

// One reading that should go into the log (already filtered by the poller's logging policy).
struct ElmLogItem {
  uint32_t tMs;
  uint8_t  pid;          // mode 01 PID, or 0xF0 for the adapter voltage
  uint8_t  status;       // 0 OK, 1 NO DATA, 2 error (same codes as the log format)
  uint8_t  nbytes;       // data bytes in raw (0 unless OK)
  uint8_t  raw[4];       // as the car sent them; the voltage is millivolts, big-endian
};

void     elmStart();                          // creates the task; call once from setup()
void     elmGetSnapshot(ElmSnapshot& out);    // thread-safe copy
bool     elmPopLog(ElmLogItem& out);          // next reading for the log; false when none are waiting
uint32_t elmQueueDrops();                     // readings lost because the log queue was full
