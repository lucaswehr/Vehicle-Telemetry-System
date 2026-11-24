#include <unity.h>
#include <string.h>
#include "dashmodel.h"

void setUp() {}
void tearDown() {}

static const uint32_t NOW = 100000;

// A fully healthy driving snapshot, every reading fresh.
static DashInput healthy() {
  DashInput in;
  sampleSet(in.obdSpeedKmh, 100.0f, NOW);        // 62.1 mph
  sampleSet(in.gpsSpeedMph, 60.0f, NOW);
  sampleSet(in.rpm, 2500.0f, NOW);
  sampleSet(in.throttlePct, 20.4f, NOW);
  sampleSet(in.loadPct, 45.0f, NOW);
  sampleSet(in.coolantC, 90.0f, NOW);            // 194 F
  sampleSet(in.volts, 14.1f, NOW);
  sampleSet(in.altM, 740.0f, NOW);
  sampleSet(in.lat, 46.7333f, NOW);
  sampleSet(in.lon, -117.1697f, NOW);
  sampleSet(in.headingDeg, 84.0f, NOW);
  in.gps = GpsStatus::OK; in.sats = 9;
  in.sd = SdState::LOGGING; in.sdFreeKnown = true; in.sdFreeMb = 15000;
  in.elm = ElmLink::LIVE;
  strcpy(in.city, "Pullman"); in.cityKnown = true;
  return in;
}

static DashPage build(const DashInput& in, uint8_t page, uint32_t now = NOW) {
  DashPage p; dashBuild(in, now, page, p); return p;
}
static const DashField* find(const DashPage& p, const char* label) {
  for (uint8_t i = 0; i < p.count; i++) if (strcmp(p.f[i].label, label) == 0) return &p.f[i];
  return nullptr;
}
static void expectField(const DashPage& p, const char* label, const char* value, Level lv) {
  const DashField* f = find(p, label);
  TEST_ASSERT_TRUE(f != nullptr);
  if (!f) return;
  TEST_ASSERT_EQUAL_STRING(value, f->value);
  TEST_ASSERT_EQUAL((int)lv, (int)f->level);
}

// ---- samples ----
void test_sample_fresh_and_stale() {
  Sample s;
  TEST_ASSERT_FALSE(sampleFresh(s, 0, 2000));             // never set
  sampleSet(s, 5, 1000);
  TEST_ASSERT_TRUE(sampleFresh(s, 3000, 2000));           // exactly at the limit
  TEST_ASSERT_FALSE(sampleFresh(s, 3001, 2000));
}

void test_sample_clock_wrap() {
  Sample s; sampleSet(s, 5, 0xFFFFFF00u);
  TEST_ASSERT_TRUE(sampleFresh(s, 0x00000100u, 2000));    // 512 ms later, across the wrap
}

// ---- page 0: drive ----
void test_drive_page_values() {
  DashPage p = build(healthy(), 0);
  TEST_ASSERT_EQUAL_STRING("DRIVE", p.title);
  TEST_ASSERT_EQUAL(6, p.count);
  expectField(p, "SPEED", "62", Level::OK);
  expectField(p, "RPM", "2500", Level::OK);
  expectField(p, "THROTTLE", "20", Level::OK);
  expectField(p, "ENGINE LOAD", "45", Level::OK);
  expectField(p, "COOLANT", "194", Level::OK);
  expectField(p, "VOLTAGE", "14.1", Level::OK);
}

void test_missing_readings_show_dashes_never_zero() {
  DashInput in;                                            // nothing ever received
  DashPage p = build(in, 0);
  for (uint8_t i = 0; i < p.count; i++) {
    TEST_ASSERT_EQUAL_STRING("--", p.f[i].value);
    TEST_ASSERT_EQUAL((int)Level::NONE, (int)p.f[i].level);
  }
}

void test_stale_readings_show_dashes() {
  DashInput in = healthy();
  DashPage p = build(in, 0, NOW + 2500);                   // 2.5 s later: fast readings are stale
  expectField(p, "RPM", "--", Level::NONE);
  expectField(p, "THROTTLE", "--", Level::NONE);
  expectField(p, "COOLANT", "194", Level::OK);             // slow group allows more age
  expectField(p, "VOLTAGE", "14.1", Level::OK);            // voltage allows 15 s
  p = build(in, 0, NOW + 16000);
  expectField(p, "VOLTAGE", "--", Level::NONE);
}

void test_real_zero_is_shown_as_zero() {
  DashInput in = healthy();
  sampleSet(in.throttlePct, 0.0f, NOW);
  sampleSet(in.obdSpeedKmh, 0.0f, NOW);
  DashPage p = build(in, 0);
  expectField(p, "THROTTLE", "0", Level::OK);              // a real 0 is not a missing value
  expectField(p, "SPEED", "0", Level::OK);
}

// ---- speed source ----
void test_speed_prefers_obd() {
  float mph;
  TEST_ASSERT_TRUE(dashSpeedMph(healthy(), NOW, mph));
  TEST_ASSERT_FLOAT_WITHIN(0.1f, 62.14f, mph);
}

void test_speed_falls_back_to_gps() {
  DashInput in = healthy();
  in.obdSpeedKmh = Sample();                               // no OBD
  float mph;
  TEST_ASSERT_TRUE(dashSpeedMph(in, NOW, mph));
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 60.0f, mph);
}

void test_speed_not_from_gps_without_fix() {
  DashInput in = healthy();
  in.obdSpeedKmh = Sample();
  in.gps = GpsStatus::NO_FIX;
  float mph = -1;
  TEST_ASSERT_FALSE(dashSpeedMph(in, NOW, mph));
  TEST_ASSERT_FLOAT_WITHIN(0.01f, -1.0f, mph);             // untouched on failure
}

void test_speed_stale_obd_falls_to_gps() {
  DashInput in = healthy();
  sampleSet(in.gpsSpeedMph, 33.0f, NOW + 2500);
  float mph;
  TEST_ASSERT_TRUE(dashSpeedMph(in, NOW + 2500, mph));     // OBD is 2.5 s old, GPS is fresh
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 33.0f, mph);
}

// ---- alert levels ----
void test_coolant_levels() {
  TEST_ASSERT_EQUAL((int)Level::OK,   (int)levelCoolantF(224.9f));
  TEST_ASSERT_EQUAL((int)Level::WARN, (int)levelCoolantF(225.0f));
  TEST_ASSERT_EQUAL((int)Level::WARN, (int)levelCoolantF(239.9f));
  TEST_ASSERT_EQUAL((int)Level::BAD,  (int)levelCoolantF(240.0f));
}

void test_coolant_alert_on_page() {
  DashInput in = healthy();
  sampleSet(in.coolantC, 110.0f, NOW);                     // 230 F
  expectField(build(in, 0), "COOLANT", "230", Level::WARN);
  sampleSet(in.coolantC, 120.0f, NOW);                     // 248 F
  expectField(build(in, 0), "COOLANT", "248", Level::BAD);
}

void test_rpm_levels() {
  TEST_ASSERT_EQUAL((int)Level::OK,   (int)levelRpm(5499));
  TEST_ASSERT_EQUAL((int)Level::WARN, (int)levelRpm(5500));
  TEST_ASSERT_EQUAL((int)Level::BAD,  (int)levelRpm(6200));
}

void test_voltage_levels_engine_running() {
  TEST_ASSERT_EQUAL((int)Level::OK,   (int)levelVoltage(14.1f, true));
  TEST_ASSERT_EQUAL((int)Level::WARN, (int)levelVoltage(12.9f, true));
  TEST_ASSERT_EQUAL((int)Level::BAD,  (int)levelVoltage(11.9f, true));
  TEST_ASSERT_EQUAL((int)Level::WARN, (int)levelVoltage(15.5f, true));   // overcharging
}

void test_voltage_levels_engine_off() {
  TEST_ASSERT_EQUAL((int)Level::OK,   (int)levelVoltage(12.6f, false));
  TEST_ASSERT_EQUAL((int)Level::WARN, (int)levelVoltage(12.1f, false));
  TEST_ASSERT_EQUAL((int)Level::BAD,  (int)levelVoltage(11.7f, false));
  TEST_ASSERT_EQUAL((int)Level::OK,   (int)levelVoltage(15.5f, false));  // not judged as overcharge when off
}

void test_voltage_judged_by_engine_state_on_page() {
  DashInput in = healthy();
  sampleSet(in.volts, 12.6f, NOW);
  expectField(build(in, 0), "VOLTAGE", "12.6", Level::WARN);   // engine running, 12.6 V is too low
  in.rpm = Sample();                                            // key off: no RPM
  expectField(build(in, 0), "VOLTAGE", "12.6", Level::OK);      // fine for a resting battery
}

void test_engine_running_needs_fresh_rpm() {
  DashInput in = healthy();
  TEST_ASSERT_TRUE(dashEngineRunning(in, NOW));
  TEST_ASSERT_FALSE(dashEngineRunning(in, NOW + 5000));         // stale RPM
  sampleSet(in.rpm, 0.0f, NOW);
  TEST_ASSERT_FALSE(dashEngineRunning(in, NOW));                // key on, engine off
}

// ---- conversions ----
void test_conversions() {
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 32.0f, cToF(0));
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 212.0f, cToF(100));
  TEST_ASSERT_FLOAT_WITHIN(0.01f, -40.0f, cToF(-40));
  TEST_ASSERT_FLOAT_WITHIN(0.1f, 3280.8f, mToFt(1000));
}

void test_compass() {
  TEST_ASSERT_EQUAL_STRING("N",  compassPoint(0));
  TEST_ASSERT_EQUAL_STRING("N",  compassPoint(22.4f));
  TEST_ASSERT_EQUAL_STRING("NE", compassPoint(22.6f));
  TEST_ASSERT_EQUAL_STRING("E",  compassPoint(90));
  TEST_ASSERT_EQUAL_STRING("S",  compassPoint(180));
  TEST_ASSERT_EQUAL_STRING("W",  compassPoint(270));
  TEST_ASSERT_EQUAL_STRING("NW", compassPoint(315));
  TEST_ASSERT_EQUAL_STRING("N",  compassPoint(359.9f));
  TEST_ASSERT_EQUAL_STRING("NW", compassPoint(-45));
}

// ---- page 1: location ----
void test_location_page_values() {
  DashInput in = healthy();
  tripUpdate(in.trip, 30, NOW - 1000);
  tripUpdate(in.trip, 36, NOW);
  DashPage p = build(in, 1);
  TEST_ASSERT_EQUAL_STRING("LOCATION", p.title);
  TEST_ASSERT_EQUAL(8, p.count);
  expectField(p, "ALTITUDE", "2428", Level::OK);               // 740 m
  expectField(p, "LATITUDE", "46.73330 N", Level::OK);
  expectField(p, "LONGITUDE", "117.16970 W", Level::OK);
  expectField(p, "CITY", "Pullman", Level::OK);
  expectField(p, "HEADING", "84 E", Level::OK);
  expectField(p, "TRIP TIME", "0:00:01", Level::OK);
}

void test_southern_and_eastern_hemispheres() {
  DashInput in = healthy();
  sampleSet(in.lat, -33.8688f, NOW);
  sampleSet(in.lon, 151.2093f, NOW);
  DashPage p = build(in, 1);
  expectField(p, "LATITUDE", "33.86880 S", Level::OK);
  expectField(p, "LONGITUDE", "151.20930 E", Level::OK);
}

void test_location_hidden_without_a_fix() {
  DashInput in = healthy();
  in.gps = GpsStatus::NO_FIX;
  DashPage p = build(in, 1);
  expectField(p, "ALTITUDE", "--", Level::NONE);
  expectField(p, "LATITUDE", "--", Level::NONE);
  expectField(p, "LONGITUDE", "--", Level::NONE);
  expectField(p, "CITY", "--", Level::NONE);
  expectField(p, "HEADING", "--", Level::NONE);
}

void test_location_stale_position_hidden() {
  DashInput in = healthy();
  DashPage p = build(in, 1, NOW + 4000);
  expectField(p, "LATITUDE", "--", Level::NONE);
}

void test_city_unknown_shows_dashes() {
  DashInput in = healthy();
  in.cityKnown = false;
  expectField(build(in, 1), "CITY", "--", Level::NONE);
}

void test_negative_altitude() {
  DashInput in = healthy();
  sampleSet(in.altM, -10.0f, NOW);
  expectField(build(in, 1), "ALTITUDE", "-33", Level::OK);
}

// ---- trip statistics ----
void test_trip_not_started_shows_dashes() {
  DashInput in = healthy();
  DashPage p = build(in, 1);
  expectField(p, "TRIP", "--", Level::NONE);
  expectField(p, "TOP SPEED", "--", Level::NONE);
  expectField(p, "TRIP TIME", "--", Level::NONE);
}

void test_trip_starts_when_moving_and_integrates_distance() {
  TripStats t;
  tripUpdate(t, 1.0f, 0);                                       // below the start speed
  TEST_ASSERT_FALSE(t.started);
  uint32_t now = 1000;
  tripUpdate(t, 60.0f, now);                                    // starts here
  TEST_ASSERT_TRUE(t.started);
  for (int i = 0; i < 60; i++) { now += 1000; tripUpdate(t, 60.0f, now); }   // 60 s at 60 mph
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 1.0f, t.miles);               // one mile
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 60.0f, t.topMph);
}

void test_trip_ignores_long_gaps() {
  TripStats t;
  tripUpdate(t, 60.0f, 1000);
  tripUpdate(t, 60.0f, 2000);                                   // +1 s counted
  tripUpdate(t, 60.0f, 62000);                                  // 60 s silence: not counted
  TEST_ASSERT_FLOAT_WITHIN(0.001f, 60.0f / 3600.0f, t.miles);
}

void test_trip_top_speed_and_reset() {
  TripStats t;
  tripUpdate(t, 30, 0); tripUpdate(t, 71, 1000); tripUpdate(t, 40, 2000);
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 71.0f, t.topMph);
  tripReset(t);
  TEST_ASSERT_FALSE(t.started);
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 0.0f, t.miles);
}

void test_trip_time_format() {
  DashInput in = healthy();
  in.trip.started = true; in.trip.startMs = NOW - (3600u + 125u) * 1000u;   // 1 h 2 min 5 s
  expectField(build(in, 1), "TRIP TIME", "1:02:05", Level::OK);
}

// ---- page 2: system ----
void test_system_page_healthy() {
  DashPage p = build(healthy(), 2);
  TEST_ASSERT_EQUAL_STRING("SYSTEM", p.title);
  TEST_ASSERT_EQUAL(5, p.count);
  expectField(p, "SD CARD", "LOGGING", Level::OK);
  expectField(p, "SD FREE", "14.6 GB", Level::OK);
  expectField(p, "ELM327", "LIVE", Level::OK);
  expectField(p, "GPS", "FIX", Level::OK);
  expectField(p, "SATELLITES", "9", Level::OK);
}

void test_sd_states() {
  DashInput in = healthy();
  in.sd = SdState::UNMOUNTED; expectField(build(in, 2), "SD CARD", "DOWN", Level::BAD);
  expectField(build(in, 2), "SD FREE", "--", Level::NONE);       // no free space shown with no card
  in.sd = SdState::ARMED;     expectField(build(in, 2), "SD CARD", "READY", Level::INFO);
  in.sd = SdState::FULL;      expectField(build(in, 2), "SD CARD", "FULL", Level::BAD);
}

void test_sd_free_formatting_and_levels() {
  DashInput in = healthy();
  in.sdFreeMb = 512;  expectField(build(in, 2), "SD FREE", "512 MB", Level::OK);
  in.sdFreeMb = 150;  expectField(build(in, 2), "SD FREE", "150 MB", Level::WARN);
  in.sdFreeMb = 40;   expectField(build(in, 2), "SD FREE", "40 MB", Level::BAD);
  in.sdFreeKnown = false;
  expectField(build(in, 2), "SD FREE", "--", Level::NONE);
}

void test_elm_states() {
  DashInput in = healthy();
  in.elm = ElmLink::OFF;        expectField(build(in, 2), "ELM327", "OFF", Level::WARN);
  in.elm = ElmLink::CONNECTING; expectField(build(in, 2), "ELM327", "CONNECTING", Level::INFO);
  in.elm = ElmLink::KEY_OFF;    expectField(build(in, 2), "ELM327", "KEY OFF", Level::INFO);
  in.elm = ElmLink::DOWN;       expectField(build(in, 2), "ELM327", "LOST", Level::BAD);
}

void test_gps_states_and_satellites() {
  DashInput in = healthy();
  in.gps = GpsStatus::NO_FIX; in.sats = 4;
  expectField(build(in, 2), "GPS", "NO FIX", Level::WARN);
  expectField(build(in, 2), "SATELLITES", "4", Level::WARN);
  in.sats = 2;
  expectField(build(in, 2), "SATELLITES", "2", Level::BAD);
  in.gps = GpsStatus::LINK_LOST;
  expectField(build(in, 2), "GPS", "LOST", Level::BAD);
  expectField(build(in, 2), "SATELLITES", "--", Level::NONE);   // no module, no count
}

// ---- structure ----
void test_unknown_page_number_falls_back_to_system() {
  DashPage p = build(healthy(), 9);
  TEST_ASSERT_EQUAL_STRING("SYSTEM", p.title);
}

void test_text_always_terminated_and_fits() {
  DashInput in = healthy();
  memset(in.city, 'A', sizeof in.city - 1);                      // longest possible city name
  in.city[sizeof in.city - 1] = 0;
  for (uint8_t pg = 0; pg < DASH_PAGES; pg++) {
    DashPage p = build(in, pg);
    TEST_ASSERT_TRUE(p.count <= DASH_MAX_FIELDS);
    for (uint8_t i = 0; i < p.count; i++) {
      TEST_ASSERT_TRUE(strlen(p.f[i].label) < sizeof p.f[i].label);
      TEST_ASSERT_TRUE(strlen(p.f[i].value) < sizeof p.f[i].value);
    }
  }
  char expect[18]; memset(expect, 'A', 17); expect[17] = 0;     // a long name is cut to the field width
  expectField(build(in, 1), "CITY", expect, Level::OK);
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_sample_fresh_and_stale);
  RUN_TEST(test_sample_clock_wrap);
  RUN_TEST(test_drive_page_values);
  RUN_TEST(test_missing_readings_show_dashes_never_zero);
  RUN_TEST(test_stale_readings_show_dashes);
  RUN_TEST(test_real_zero_is_shown_as_zero);
  RUN_TEST(test_speed_prefers_obd);
  RUN_TEST(test_speed_falls_back_to_gps);
  RUN_TEST(test_speed_not_from_gps_without_fix);
  RUN_TEST(test_speed_stale_obd_falls_to_gps);
  RUN_TEST(test_coolant_levels);
  RUN_TEST(test_coolant_alert_on_page);
  RUN_TEST(test_rpm_levels);
  RUN_TEST(test_voltage_levels_engine_running);
  RUN_TEST(test_voltage_levels_engine_off);
  RUN_TEST(test_voltage_judged_by_engine_state_on_page);
  RUN_TEST(test_engine_running_needs_fresh_rpm);
  RUN_TEST(test_conversions);
  RUN_TEST(test_compass);
  RUN_TEST(test_location_page_values);
  RUN_TEST(test_southern_and_eastern_hemispheres);
  RUN_TEST(test_location_hidden_without_a_fix);
  RUN_TEST(test_location_stale_position_hidden);
  RUN_TEST(test_city_unknown_shows_dashes);
  RUN_TEST(test_negative_altitude);
  RUN_TEST(test_trip_not_started_shows_dashes);
  RUN_TEST(test_trip_starts_when_moving_and_integrates_distance);
  RUN_TEST(test_trip_ignores_long_gaps);
  RUN_TEST(test_trip_top_speed_and_reset);
  RUN_TEST(test_trip_time_format);
  RUN_TEST(test_system_page_healthy);
  RUN_TEST(test_sd_states);
  RUN_TEST(test_sd_free_formatting_and_levels);
  RUN_TEST(test_elm_states);
  RUN_TEST(test_gps_states_and_satellites);
  RUN_TEST(test_unknown_page_number_falls_back_to_system);
  RUN_TEST(test_text_always_terminated_and_fits);
  return UNITY_END();
}
