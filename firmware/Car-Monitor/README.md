# Vehicle Telemetry & Drive Logger

ESP32 firmware that logs GPS data from a 2001 Ford Taurus to a microSD card in a compact binary
format with per-record CRCs, plus a Python tool that verifies and summarizes the logs.
It is being built in stages: a single-loop baseline first, then more sensors, then FreeRTOS tasks,
with measurements at each step.

**Status:** GPS and SD logging work on the bench and on two short test drives (both logs read back
with no CRC failures, sequence gaps or stalls). ELM327 polling, the 3.5" display and the combined "car" firmware are written and unit-tested on the PC but
not yet verified in the car. Not yet built: IMU, barometer, FreeRTOS split of the main loop, battery-drain alarm.

## What works today

- NMEA parser (RMC/GGA) with checksum validation, and a link/fix status state machine
  (`LINK_LOST` / `NO_FIX` / `OK`)
- u-blox NEO-7M configured over UBX to 5 Hz, with unused sentences switched off to fit 9600 baud
- Binary log format with sequence numbers, timestamps and CRC-16, one file per trip
- A trip file is created only when the car is moving: a valid fix and a speed above 5 mph for 5
  records in a row. The last 5 s before that are kept in RAM and written first, so the start of the
  drive is not lost. Sitting parked, or creeping below 5 mph, creates no files
- SD failure handling: retry with backoff if the card is missing, drop and recover if it is
  removed mid-trip, count every record that could not be saved
- Trip files named by local date at creation (`2026-11-08_trip2.bin`), with US daylight saving
- Low-space handling: free space is measured when the card mounts; below 50 MB the oldest trip
  files are deleted (only files with this logger's names) until 100 MB is free, and logging stops
  cleanly (`FULL`) if the card runs out
- Timing instrumentation (loop period, SD flush time, mount stalls)
- `tools/read_trip.py`: verifies every CRC, detects sequence gaps, stalls and truncated tails,
  reports distance/speed, exports CSV, plots the route
- Unit tests for all of the pure logic, run on the PC (no board needed)

## Hardware

| Part | Connection |
|---|---|
| ESP32 DevKit V1 (original ESP32) | USB (CP2102) |
| u-blox NEO-7M GPS | UART2: GPS TX to GPIO16 (RX), GPS RX to GPIO17 (TX), 9600 baud |
| microSD module | SPI (VSPI): CS GPIO5, SCK 18, MISO 19, MOSI 23. **Power from VIN (5 V)**, see bug log |
| Hosyond 3.5" IPS 320x480, ST7796U + FT6336U touch | Own SPI bus (HSPI): CS 15, RESET 26, DC 27, MOSI 13, SCK 14, backlight 25, MISO unused. Touch (I2C): SDA 21, SCL 22, RST 4, INT 35. **VCC to VIN (5 V)**. Needs `-DTFT_INVERSION_ON=1` (colors are inverted otherwise) |
| ELM327 Bluetooth Classic adapter | Bluetooth, no pins. Address and PIN are set at the top of `src/elm327.cpp` |

## Build, flash, test

```
pio run -e esp32dev -t upload     # build and flash the logger
pio run -e bench -t upload        # same, but logs without waiting for movement (desk testing)
pio device monitor                # serial status at 115200
pio test -e native                # unit tests on the PC (run in WSL on Windows)
pio run -e sd_test -t upload      # standalone SD card bring-up test
pio run -e elm_test -t upload     # ELM327 Bluetooth bring-up test (run in the car)
pio run -e display_test -t upload # display colors, orientation and touch check
pio run -e dash_demo -t upload    # the three dashboard pages with fake data (tap to change page)
pio run -e car -t upload          # everything together: GPS + SD log + OBD-II + screen
```

The serial monitor prints two lines per second: GPS status, SD state (`READY (waiting for
movement)`, `LOGGING`, `DOWN` or `FULL`), estimated free space, records, dropped records, write
errors, remounts, and a `perf` line with loop and flush timings.

## Reading a log

```
python tools/read_trip.py D:\2026-11-08_trip2.bin            # health report and summary
python tools/read_trip.py D:\2026-11-08_trip2.bin --csv out.csv
python tools/read_trip.py D:\2026-11-08_trip2.bin --plot      # needs matplotlib
python tools/make_test_trip.py test.bin                       # synthetic file with known damage
```

`make_test_trip.py` generates a file with a flipped bit, garbage bytes, dropped records, a stall and
a truncated last record, so the reader can be checked against known answers.

## Log format (version 1, little-endian)

File header: `T L O G` + version byte (5 bytes).

| Offset | Size | Field |
|---|---|---|
| 0 | 1 | magic `0xA5` |
| 1 | 1 | record type (1 = GPS, 2 = IMU (reserved), 3 = OBD) |
| 2 | 1 | payload length |
| 3 | 4 | sequence number |
| 7 | 4 | `millis()` since boot |
| 11 | n | payload |
| 11+n | 2 | CRC-16/CCITT-FALSE over everything before it |

GPS payload (20 bytes, 33 bytes per record in total): latitude and longitude ×1e7 (i32),
speed in 0.01 mph (u16), altitude in 0.1 m (i16), heading in 0.1° (u16), satellites (u8),
status (u8: 0 link lost, 1 no fix, 2 OK), GPS time of day in ms (u32). Position and speed are
written as zero unless the status is OK, so a bad fix can never look like a real position.

OBD payload (7 bytes, 20 bytes per record in total): PID (u8), status (u8: 0 OK, 1 NO DATA, 2 error),
number of data bytes (u8), then 4 raw bytes exactly as the car sent them (unused bytes are zero).
The raw bytes are converted to units by the reader, so a formula fix never needs a firmware change.
Only an OK record carries data: NO DATA and errors have zero data bytes, so a missing reading can
never be mistaken for a value of 0. The adapter's own battery voltage uses pseudo-PID `0xF0`
(millivolts, big-endian). Run `read_trip.py file.bin --obd-csv out.csv` to get one row per reading.

A reader that hits a bad record skips one byte and looks for the next magic byte, so a damaged
region costs only the records inside it.

## Code layout

```
lib/      pure logic, no Arduino calls, unit-tested on the PC
  nmea/ gps_status/ ubx/ logfmt/ sdstate/ tripname/ metrics/ movetrigger/ preroll/
  elm_parse/ elm_poll/ dashmodel/ places/
src/      hardware code: gps.cpp, logger.cpp, main.cpp (logger only), main_full.cpp (the car firmware),
          elm327.cpp (Bluetooth task), display_ui.cpp (screen), plus the bring-up sketches
test/     one folder per library (Unity)
tools/    read_trip.py, make_test_trip.py, make_places.py
docs/     baseline_results.md
```

Decisions that need an SD card or a UART live in `src/`; everything with a rule in it (parsing,
the SD retry/backoff state machine, the start-of-drive trigger, file naming, statistics) lives in `lib/` so it can be tested
without hardware.

## Measured results (single-loop baseline)

Details, setup and caveats are in [`docs/baseline_results.md`](docs/baseline_results.md).

- Normal operation: loop pass about 13 µs, SD flush 15-20 ms once a second, no dropped records.
- Card pulled mid-trip: first failed flush blocked the loop for 3.05 s, the next two for 1.01 s
  each. Failure was detected about 6.7 s after the pull. GPS reported `LINK LOST` during the stall.
- Card missing: every mount retry blocks the loop for about 1 s.

These stalls are what the FreeRTOS version will be measured against.

## Bug log

- **SD card would not mount** (`Card Failed! cmd: 0x00`, `Select Failed`). The module has an onboard
  3.3 V regulator that needs 5 V input. Powering it from the 3.3 V pin did not work; moving VCC to VIN did.
- **GPS "link lost" never triggered.** A disconnected RX pin picks up noise, and any received byte
  was resetting the timeout. Fixed by counting only checksum-valid NMEA sentences as proof the
  module is talking.
- **Stationary GPS drift.** With 5 satellites and the module sitting perfectly still, the position
  wandered about 60 m and speed spiked to 4.6 mph. The first version of the distance calculation
  added up those jumps. It now integrates speed above 5 mph and shows raw path length separately.
- **SD write errors went unnoticed.** After the card was pulled, `write()` and `flush()` still
  reported success (the data only reached a RAM cache). Found by the pull test; failure is now
  detected by checking the file against the card after each flush.
- **A test that tested nothing.** `test_no_fix_is_invalid` passed because its sample sentence had a bad
  checksum, so parsing failed and the default (invalid) value was returned. Fixed the checksum
  and made the test assert that the parse succeeds.
- **The timing code hid the worst stalls.** The loop timer skipped the pass that printed the status,
  but after any stall longer than a second the print fires in that same pass, so the biggest
  stalls were exactly the samples dropped. Now the print time is subtracted instead.
- **Trip names had tomorrow's date.** The GPS reports UTC, so evening trips in Pacific time were
  labeled with the next day. File names now use a local date computed from the zone's UTC offset,
  with the US daylight saving rules applied. The name is chosen when the file is created (which now
  happens once the car is moving, so the GPS date is always valid), so there is no rename step.

## Dashboard and live OBD data

The `car` build runs the ELM327 in its own FreeRTOS task (core 0), so connecting and waiting for the
adapter never stalls GPS, SD logging or the screen. `lib/elm_poll` decides what to ask (RPM, speed,
throttle and load every cycle, one slower PID per cycle, battery voltage every 10 s), spots key-off
from a run of NO DATA and reconnects with backoff. `lib/dashmodel` turns the readings into the text
and alert colors for three portrait pages: DRIVE (speed, RPM, throttle, load, coolant, voltage),
LOCATION (altitude, lat/lon, nearest city, heading, trip distance, top speed, trip time) and
SYSTEM (SD, ELM327, GPS, satellites). A missing or stale reading shows `--`, never 0. Alert
thresholds are constants at the top of `lib/dashmodel/dashmodel.h`. The city comes from an offline
table built by `tools/make_places.py` from a GeoNames file (`cities1000.txt`, US places).

## Known limitations

- File-name dates assume the Pacific time zone (`LOCAL_STD_OFFSET_HOURS` in `src/logger.cpp`); US daylight saving is applied automatically.
- The 5 Hz GPS setting is sent once at boot and not verified. During bring-up it was seen to fail
  silently on a cold boot.
- `SD.begin()` blocks for about a second while looking for a missing card, which stalls GPS reading.
- Nothing is logged below 5 mph, so a slow crawl or a stay in a parking lot is not recorded. Once a
  trip has started it keeps logging until power-off, including stops.
- While waiting for movement nothing is written, so a card pulled while parked goes unnoticed until
  the car moves; the first write then fails and the normal retry takes over.
- Free space is read once when the card mounts, because on a large FAT card that call can block for
  seconds (not yet measured on this hardware). Space used afterwards is tracked by counting bytes.
- The low-space cleanup deletes the oldest trips without asking, so copy trips off the card
  you want to keep.
- Pulling the card mid-write can corrupt the card's file system. The firmware survives it; the card may not.
- One card and one SD module have been tested, on the bench and on two short drives.

## Roadmap

1. Verify the `car` firmware in the car, then re-measure loop timing against the baseline
2. IMU (MPU6050) and barometer over I2C, new log record types
3. Move the remaining loop work (GPS, logging, display) into FreeRTOS tasks and compare with the baseline
4. Parked-battery-drain alarm with deep sleep
