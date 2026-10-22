# Car Health Monitor and Drive Logger

An ESP32-based device for a 2001 Ford Taurus that reads GPS, motion, and engine data, logs it reliably, and shows the useful parts on a small display. The product is the car monitor, but the point of the project is the firmware engineering underneath it.

Hardware: ESP32 DevKit V1, NEO-7M GPS, MPU6050 IMU, ELM327 Bluetooth OBD-II adapter, microSD module, small SPI display. Everything is prototyped on a breadboard at my desk first.

Rule for this README: a feature is only marked done once it works, and every phase ends with measured results.

## Phase 1: Sensors and the ELM327 Check

**Goal:** Get raw, trustworthy data from each part, one at a time.

- Confirm the ELM327 can talk to the Taurus (its older J1850 protocol) using a phone app, before involving the ESP32. Use the return window if it can’t.
- Read the IMU over I2C, find its address with a scanner, and print raw acceleration and rotation.
- Read the GPS over UART, print raw NMEA text near a window, and wait for a first fix.

**Done when:** All three produce data, and I know how long the GPS takes to get a first fix.

## Phase 2: Drivers and Parsing

**Goal:** Turn raw data into clean values using code I wrote.

- Parse NMEA sentences with checksum validation, and handle partial or garbled lines.
- Read IMU registers from the datasheet, convert to real units, and calibrate gyro bias.
- Filter the IMU data so tilt and acceleration are stable.
- Raise the GPS update rate from 1 Hz to 5 Hz.

**Done when:** Speed, position, tilt, and acceleration print as clean numbers, and a bad GPS sentence is rejected instead of crashing the parser.

## Phase 3: Task Architecture

**Goal:** Run everything at once without tasks interfering.

- Split the firmware into FreeRTOS tasks: fast IMU, slower GPS, logger, and display.
- Pass data between tasks with queues, and protect shared state properly.
- Measure sensor loop jitter, queue depth, and stack high-water marks.

**Done when:** The IMU loop holds its rate while other tasks run, and I can show the jitter numbers.

## Phase 4: Logging and the Log Format

**Goal:** Store data reliably, in a format I designed.

- Define a binary record format with header, timestamp, sequence number, and CRC.
- Write to the SD card from a low-priority logger task so slow writes never block sensing.
- Write in blocks so a power cut loses seconds, not a whole trip.
- Build a Python tool that parses the logs, checks CRCs and dropped records, and plots each trip.

**Done when:** I can pull the power mid-run and still read every record except the last few seconds, and the plots match what actually happened.

## Phase 5: Events and Trip Data

**Goal:** Make the data meaningful.

- Detect hard braking, hard acceleration, bumps, and stops from filtered IMU data, with GPS location.
- Keep a ring buffer so the seconds before an event are saved too.
- Compute trip stats and a smoothness score, plus 0 to 60 time (only on a closed course).

**Done when:** A hard stop in a parking lot shows up as a logged event with the right time and place.

## Phase 6: Fault Handling

**Goal:** Survive failures and report them.

- Detect a dead IMU, no GPS fix, a missing or full SD card, and a hung task.
- Add a watchdog, brownout handling, and a reset-reason log kept in flash.
- Run fault-injection tests: unplug a sensor mid-run, pull the SD card, and corrupt a record.

**Done when:** Every injected fault is detected, logged, and recovered from, and the results are listed in the README.

## Phase 7: Replay Mode

**Goal:** Develop and test without the car.

- Make the firmware read recorded logs in place of live sensors.
- Record a few real drives, then build and test everything against them.

**Done when:** The full system runs at my desk from a recorded drive, with the same output as the live run.

## Phase 8: ELM327 Engine Data

**Goal:** Add what the car itself knows.

- Connect to the adapter over Classic Bluetooth, with pairing and reconnect handling.
- Build a command and response state machine for coolant temperature, RPM, battery voltage, and fault codes.
- Handle errors like no data, searching, and dropped connections.
- Test first against an ELM327 emulator at my desk.

**Done when:** The dashboard shows live coolant temperature and battery voltage in the Taurus, and survives the adapter disconnecting.

## Phase 9: Display

**Goal:** Show the useful numbers at a glance.

- Drive an SPI display with big, readable values and a button to change pages.
- Run it in its own task, redrawing only what changed.
- Dim the screen automatically at night.

**Done when:** I can read speed and battery voltage in one glance, and the display never slows the sensor tasks.

## Phase 10: Power and Parked-Drain Alarm

**Goal:** Warn me before the battery strands me, with a measured idle current.

- Track battery voltage and trend, ignoring the first 15 to 30 minutes after shutoff.
- Alert on low voltage or a steady parked drain.
- Add deep sleep that wakes about once a minute, and store readings in RTC memory.
- Measure idle current with a multimeter, then reduce it.
- Optional hardware: pigtail plug, fuse, buck converter, and a voltage divider for direct measurement.

**Done when:** I have a before and after idle current number, and the alarm fires in a bench test.

## Phase 11: Install and Final Testing

**Goal:** Run it in the Taurus for real.

- Mount the device so nothing moves, with the GPS antenna against the windshield.
- Power it from a USB car charger first.
- Drive, collect logs, and fix what breaks.

**Done when:** It runs through several real trips without losing data.

## Phase 12: Write-Up

**Goal:** Make the work easy to understand.

- Add an architecture diagram, wiring notes, and the log format spec.
- Report the measurements: task jitter, queue depth, SD write latency, record loss, time to first fix, and idle current.
- Add a bug log of what broke and how I fixed it, plus honest limitations.

**Done when:** Someone who has never seen the project can understand what I built and trust the numbers.

## Skills This Project Is Meant to Build

C/C++ on a microcontroller, I2C, SPI, and UART, FreeRTOS tasks and queues, binary protocol and file format design, sensor filtering, power management and deep sleep, fault handling, Bluetooth client work, and test tooling in Python.