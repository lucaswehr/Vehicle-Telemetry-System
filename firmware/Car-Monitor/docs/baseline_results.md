# Baseline results: single-loop firmware (before FreeRTOS)

Measured on the bench with the original single-loop design: GPS polling, record encoding and
SD writes all happen in `loop()`. These are the "before" numbers for the FreeRTOS comparison.
After the tasks version is built, repeat the same tests and fill in an "after" column.

## Setup

- ESP32 DevKit V1 (original ESP32), Arduino framework, PlatformIO
- NEO-7M GPS at 5 Hz, UART2 at 9600 baud, 1024-byte RX buffer
- microSD module on VSPI at 4 MHz, ~15 GB card, FAT32
- Log: one 33-byte GPS record per fix (5 per second), 512-byte RAM buffer, flushed once a second
- Timing taken with `micros()` inside the firmware (`lib/metrics`), printed once a second.
  The time spent printing is subtracted from the loop figures.
- "Flush" = `file.write()` + `file.flush()` + the `SD.exists()` card-presence check.

## 1. Normal operation (card in, GPS fix)

| Metric | Result |
|---|---|
| Loop pass, average | ~13 µs |
| SD flush, typical | 15-20 ms, once per second |
| SD flush, worst in steady state | 20.1 ms |
| Records dropped | 0 |
| CRC failures / sequence gaps (checked with `tools/read_trip.py`) | 0 / 0 |

A flush blocks the loop for its whole duration, so the worst loop pass in a window equals
the flush time (15-20 ms). That is small next to the 200 ms GPS interval.

## 2. Card removed mid-trip (2 runs)

| Metric | Result |
|---|---|
| Longest single flush stall | **3.05 s** (first failed flush) |
| Next two failed flushes | 1.01 s each |
| Time from pull to `sd DOWN` | ~6.7 s (3 failed flushes) |
| Buffered records lost at unmount | 16 |
| Records lost while the card is out | ~5 per second (every GPS update) |
| GPS status during the 3 s stall | `LINK LOST` (loop was not reading the GPS UART) |
| GPS back to `FIX OK` | ~9-10 s after the pull |

## 3. Card out, mount retries

| Metric | Result |
|---|---|
| Blocking time per failed mount attempt (`SD.begin()`) | ~1.0 s (one window 1.5 s) |
| Retry spacing | 3, 5, 9, then ~11 s (backoff + the 1 s stall) |
| Loop between retries | ~300 µs worst case |

## 4. Card reinserted

| Metric | Result |
|---|---|
| Recovery | automatic, on the next retry (up to ~10 s after insertion) |
| Loop stall during recovery (mount + open + header + date rename) | ~350 ms |
| First flush on the new file | 23 ms |
| New file | created, named with the GPS date (UTC) |

## What this shows

- With a healthy card the design is fine: 15-20 ms stalls once a second.
- A failing card freezes the whole program for seconds (3 s, then 1 s stalls and 1 s retry stalls)
  and starves GPS reading long enough to lose the link status.
- Moving SD access into its own low-priority task should keep the GPS loop running during all of
  the stalls in sections 2 and 3. That is what the FreeRTOS version will be measured against.

## Caveats

- Small samples: two card pulls, one reinsertion, about a minute of steady state each.
- One card, one SD module, bench wiring, not yet in the car.
- Steady-state flush time varied between sessions (about 15 ms in one, 17-20 ms in another). The cause was not investigated.
- Dates in file names are UTC, so evening trips in Pacific time get the next day's date.
