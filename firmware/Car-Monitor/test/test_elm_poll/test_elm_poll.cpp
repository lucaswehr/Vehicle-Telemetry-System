#include <unity.h>
#include "elm_poll.h"
#include "logfmt.h"

void setUp() {}
void tearDown() {}

// ---- helpers: drive the poller the way the hardware loop does ----
static PollRequest ask(ElmPoll& p, uint32_t now) {
  PollRequest r = {PollKind::NONE, 0};
  TEST_ASSERT_TRUE(pollNext(p, now, r));
  return r;
}
static PollLog answer(ElmPoll& p, ElmStatus s, uint32_t now) {
  PollLog l;
  pollOnResult(p, s, now, l);
  return l;
}
// ask + answer; returns the request that was made
static PollRequest step(ElmPoll& p, ElmStatus s, uint32_t now) {
  PollRequest r = ask(p, now);
  answer(p, s, now);
  return r;
}
static ElmPoll fresh() { ElmPoll p; pollInit(p); return p; }
static void takeVoltage(ElmPoll& p, uint32_t now) { step(p, ElmStatus::OK, now); }

// ---- schedule ----
void test_first_request_is_voltage() {
  ElmPoll p = fresh();
  PollRequest r = ask(p, 0);
  TEST_ASSERT_EQUAL((int)PollKind::VOLTAGE, (int)r.kind);
  TEST_ASSERT_EQUAL_UINT8(POLL_PID_VOLTAGE, r.pid);
}

void test_fast_group_then_one_slow_pid() {
  ElmPoll p = fresh(); takeVoltage(p, 0);
  const uint8_t expect[] = {0x0C, 0x0D, 0x11, 0x04, 0x05,    // cycle 1: fast x4, slow coolant
                            0x0C, 0x0D, 0x11, 0x04, 0x0F,    // cycle 2: intake air
                            0x0C, 0x0D, 0x11, 0x04, 0x06,    // cycle 3: short trim
                            0x0C, 0x0D, 0x11, 0x04, 0x07,    // cycle 4: long trim
                            0x0C, 0x0D, 0x11, 0x04, 0x05};   // wraps back to coolant
  for (size_t i = 0; i < sizeof expect; i++) {
    PollRequest r = step(p, ElmStatus::OK, 100 + i);
    TEST_ASSERT_EQUAL((int)PollKind::PID, (int)r.kind);
    TEST_ASSERT_EQUAL_UINT8(expect[i], r.pid);
  }
}

void test_no_new_request_while_one_is_out() {
  ElmPoll p = fresh();
  ask(p, 0);
  PollRequest r;
  TEST_ASSERT_FALSE(pollNext(p, 5, r));
}

void test_unrequested_result_is_ignored() {
  ElmPoll p = fresh();
  PollLog l = answer(p, ElmStatus::OK, 0);
  TEST_ASSERT_FALSE(l.log);
}

void test_voltage_every_10_seconds() {
  ElmPoll p = fresh(); takeVoltage(p, 0);
  PollRequest r = step(p, ElmStatus::OK, 9999);
  TEST_ASSERT_EQUAL((int)PollKind::PID, (int)r.kind);       // 9.999 s: still a PID
  r = step(p, ElmStatus::OK, 10000);
  TEST_ASSERT_EQUAL((int)PollKind::VOLTAGE, (int)r.kind);   // 10 s: voltage
  r = step(p, ElmStatus::OK, 10001);
  TEST_ASSERT_EQUAL((int)PollKind::PID, (int)r.kind);
}

void test_voltage_does_not_skip_the_fast_group() {
  ElmPoll p = fresh(); takeVoltage(p, 0);
  step(p, ElmStatus::OK, 10);                 // RPM
  step(p, ElmStatus::OK, 10000);              // voltage squeezes in
  PollRequest r = step(p, ElmStatus::OK, 10001);
  TEST_ASSERT_EQUAL_UINT8(0x0D, r.pid);       // speed comes next, nothing lost
}

// ---- logging policy ----
void test_ok_readings_always_logged() {
  ElmPoll p = fresh(); takeVoltage(p, 0);
  for (int i = 0; i < 3; i++) {
    ask(p, 10 + i);
    PollLog l = answer(p, ElmStatus::OK, 10 + i);
    TEST_ASSERT_TRUE(l.log);
    TEST_ASSERT_EQUAL_UINT8(POLL_LOG_OK, l.status);
  }
}

void test_no_data_logged_once_per_pid() {
  ElmPoll p = fresh(); takeVoltage(p, 0);
  ask(p, 1);  PollLog l = answer(p, ElmStatus::NO_DATA, 1);       // RPM
  TEST_ASSERT_TRUE(l.log);
  TEST_ASSERT_EQUAL_UINT8(POLL_LOG_NO_DATA, l.status);
  TEST_ASSERT_EQUAL_UINT8(0x0C, l.pid);
  ask(p, 2);  l = answer(p, ElmStatus::NO_DATA, 2);               // speed: its own first NO DATA
  TEST_ASSERT_TRUE(l.log);
  ask(p, 3); answer(p, ElmStatus::NO_DATA, 3);                    // throttle
  ask(p, 4); answer(p, ElmStatus::NO_DATA, 4);                    // load
  ask(p, 5); answer(p, ElmStatus::OK, 5);                         // slow PID ok: run reset
  ask(p, 6);  l = answer(p, ElmStatus::NO_DATA, 6);               // RPM again, still NO DATA
  TEST_ASSERT_FALSE(l.log);                                       // repeat: not logged
}

void test_ok_after_no_data_is_logged() {
  ElmPoll p = fresh(); takeVoltage(p, 0);
  ask(p, 1); answer(p, ElmStatus::NO_DATA, 1);
  for (int i = 0; i < 4; i++) step(p, ElmStatus::OK, 2 + i);      // finish the cycle
  ask(p, 10); PollLog l = answer(p, ElmStatus::OK, 10);
  TEST_ASSERT_TRUE(l.log);
  TEST_ASSERT_EQUAL_UINT8(POLL_LOG_OK, l.status);
}

void test_error_then_no_data_both_logged() {
  ElmPoll p = fresh(); takeVoltage(p, 0);
  ask(p, 1); PollLog l = answer(p, ElmStatus::BUS_ERROR, 1);      // RPM
  TEST_ASSERT_TRUE(l.log);
  TEST_ASSERT_EQUAL_UINT8(POLL_LOG_ERROR, l.status);
  for (int i = 0; i < 4; i++) step(p, ElmStatus::OK, 2 + i);
  ask(p, 10); l = answer(p, ElmStatus::NO_DATA, 10);              // RPM again, different failure
  TEST_ASSERT_TRUE(l.log);
  TEST_ASSERT_EQUAL_UINT8(POLL_LOG_NO_DATA, l.status);
}

void test_different_failure_kinds_map_to_error() {
  TEST_ASSERT_EQUAL_UINT8(POLL_LOG_OK, elmLogStatus(ElmStatus::OK));
  TEST_ASSERT_EQUAL_UINT8(POLL_LOG_NO_DATA, elmLogStatus(ElmStatus::NO_DATA));
  TEST_ASSERT_EQUAL_UINT8(POLL_LOG_ERROR, elmLogStatus(ElmStatus::BAD_FORMAT));
  TEST_ASSERT_EQUAL_UINT8(POLL_LOG_ERROR, elmLogStatus(ElmStatus::WRONG_PID));
  TEST_ASSERT_EQUAL_UINT8(POLL_LOG_NO_DATA, elmLogStatus(ElmStatus::UNABLE_TO_CONNECT));   // silent car
  TEST_ASSERT_EQUAL_UINT8(POLL_LOG_ERROR, elmLogStatus(ElmStatus::BUS_ERROR));
}

void test_codes_match_the_log_format() {
  TEST_ASSERT_EQUAL_UINT8(ELM_STATUS_OK, POLL_LOG_OK);
  TEST_ASSERT_EQUAL_UINT8(ELM_STATUS_NO_DATA, POLL_LOG_NO_DATA);
  TEST_ASSERT_EQUAL_UINT8(ELM_STATUS_ERROR, POLL_LOG_ERROR);
  TEST_ASSERT_EQUAL_UINT8(ELM_PID_ADAPTER_VOLTAGE, POLL_PID_VOLTAGE);
}

// ---- key off / key on ----
// Voltage taken at t=0, then NO DATA answers; `now` comes back as the time key-off was entered.
static ElmPoll keyOff(uint32_t& now) {
  ElmPoll p = fresh(); takeVoltage(p, 0);
  now = 100;
  for (int i = 0; i < POLL_KEYOFF_NODATA_RUN; i++) step(p, ElmStatus::NO_DATA, now++);
  now--;
  return p;
}

void test_unable_to_connect_means_key_off_not_a_lost_adapter() {
  ElmPoll p = fresh(); takeVoltage(p, 0);
  for (int i = 0; i < POLL_KEYOFF_NODATA_RUN; i++) step(p, ElmStatus::UNABLE_TO_CONNECT, 100 + i);
  TEST_ASSERT_EQUAL((int)PollMode::KEY_OFF, (int)pollMode(p));
  for (int i = 1; i <= 20; i++) {                                    // a minute of probes, all unanswered
    PollRequest r;
    uint32_t t = 100 + i * 3000;
    if (pollNext(p, t, r)) answer(p, ElmStatus::UNABLE_TO_CONNECT, t);
  }
  TEST_ASSERT_EQUAL((int)PollMode::KEY_OFF, (int)pollMode(p));       // never DOWN, however many arrive
}

void test_unable_to_connect_is_logged_once_as_no_data() {
  ElmPoll p = fresh(); takeVoltage(p, 0);
  ask(p, 1); PollLog l = answer(p, ElmStatus::UNABLE_TO_CONNECT, 1);
  TEST_ASSERT_TRUE(l.log);
  TEST_ASSERT_EQUAL_UINT8(POLL_LOG_NO_DATA, l.status);
}

void test_four_no_data_is_not_key_off() {
  ElmPoll p = fresh(); takeVoltage(p, 0);
  for (int i = 0; i < POLL_KEYOFF_NODATA_RUN - 1; i++) step(p, ElmStatus::NO_DATA, 100 + i);
  TEST_ASSERT_EQUAL((int)PollMode::NORMAL, (int)pollMode(p));
}

void test_five_no_data_in_a_row_is_key_off() {
  uint32_t now; ElmPoll p = keyOff(now);
  TEST_ASSERT_EQUAL((int)PollMode::KEY_OFF, (int)pollMode(p));
}

void test_an_ok_reading_breaks_the_no_data_run() {
  ElmPoll p = fresh(); takeVoltage(p, 0);
  for (int i = 0; i < 4; i++) step(p, ElmStatus::NO_DATA, 10 + i);
  step(p, ElmStatus::OK, 20);
  for (int i = 0; i < 4; i++) step(p, ElmStatus::NO_DATA, 30 + i);
  TEST_ASSERT_EQUAL((int)PollMode::NORMAL, (int)pollMode(p));
}

void test_voltage_does_not_count_toward_key_off() {
  ElmPoll p = fresh(); takeVoltage(p, 0);
  for (int i = 0; i < 4; i++) step(p, ElmStatus::NO_DATA, 10 + i);
  step(p, ElmStatus::OK, 10000);                       // voltage (OK) neither extends nor breaks the run
  step(p, ElmStatus::NO_DATA, 10001);
  TEST_ASSERT_EQUAL((int)PollMode::KEY_OFF, (int)pollMode(p));
}

void test_key_off_asks_only_voltage_and_rpm_probe() {
  uint32_t now; ElmPoll p = keyOff(now);
  PollRequest r;
  TEST_ASSERT_FALSE(pollNext(p, now + 100, r));          // nothing due yet
  TEST_ASSERT_FALSE(pollNext(p, now + 2999, r));
  r = ask(p, now + 3000);                                // the probe is due after 3 s
  TEST_ASSERT_EQUAL((int)PollKind::PID, (int)r.kind);
  TEST_ASSERT_EQUAL_UINT8(PID_RPM, r.pid);
  answer(p, ElmStatus::NO_DATA, now + 3000);
  TEST_ASSERT_EQUAL((int)PollMode::KEY_OFF, (int)pollMode(p));
}

void test_key_off_voltage_every_5_seconds() {
  uint32_t now; ElmPoll p = keyOff(now);                  // now = 104, last voltage was at 0
  step(p, ElmStatus::NO_DATA, now + 3000);                // probe at 3104
  PollRequest r;
  TEST_ASSERT_FALSE(pollNext(p, 4999, r));
  r = ask(p, 5000);                                       // voltage 5 s after the last one
  TEST_ASSERT_EQUAL((int)PollKind::VOLTAGE, (int)r.kind);
  answer(p, ElmStatus::OK, 5000);
  TEST_ASSERT_FALSE(pollNext(p, 9999, r) && r.kind == PollKind::VOLTAGE);
}

void test_key_off_probe_no_data_not_relogged() {
  uint32_t now; ElmPoll p = keyOff(now);
  ask(p, now + 3000);
  PollLog l = answer(p, ElmStatus::NO_DATA, now + 3000);
  TEST_ASSERT_FALSE(l.log);                               // RPM was already logged as NO DATA
}

void test_key_on_again_returns_to_normal_and_restarts_cycle() {
  uint32_t now; ElmPoll p = keyOff(now);
  PollRequest r = ask(p, now + 3000);
  TEST_ASSERT_EQUAL_UINT8(PID_RPM, r.pid);
  PollLog l = answer(p, ElmStatus::OK, now + 3000);       // RPM answers: key is on
  TEST_ASSERT_TRUE(l.log);
  TEST_ASSERT_EQUAL((int)PollMode::NORMAL, (int)pollMode(p));
  r = ask(p, now + 3001);
  TEST_ASSERT_EQUAL_UINT8(PID_RPM, r.pid);                // fast group restarts from RPM
}

// ---- link health and reconnect ----
void test_six_unusable_replies_take_the_link_down() {
  ElmPoll p = fresh(); takeVoltage(p, 0);
  for (int i = 0; i < POLL_LINK_FAIL_LIMIT - 1; i++) step(p, ElmStatus::BAD_FORMAT, 10 + i);
  TEST_ASSERT_EQUAL((int)PollMode::NORMAL, (int)pollMode(p));
  step(p, ElmStatus::BAD_FORMAT, 100);
  TEST_ASSERT_EQUAL((int)PollMode::DOWN, (int)pollMode(p));
  PollRequest r;
  TEST_ASSERT_FALSE(pollNext(p, 101, r));
}

void test_good_reply_resets_the_failure_count() {
  ElmPoll p = fresh(); takeVoltage(p, 0);
  for (int i = 0; i < 5; i++) step(p, ElmStatus::BAD_FORMAT, 10 + i);
  step(p, ElmStatus::OK, 20);
  for (int i = 0; i < 5; i++) step(p, ElmStatus::BAD_FORMAT, 30 + i);
  TEST_ASSERT_EQUAL((int)PollMode::NORMAL, (int)pollMode(p));
}

void test_no_data_proves_the_adapter_is_alive() {
  ElmPoll p = fresh(); takeVoltage(p, 0);
  for (int i = 0; i < 5; i++) step(p, ElmStatus::BAD_FORMAT, 10 + i);
  step(p, ElmStatus::NO_DATA, 20);                        // adapter answered
  step(p, ElmStatus::BAD_FORMAT, 21);
  TEST_ASSERT_TRUE(pollMode(p) != PollMode::DOWN);
}

void test_question_mark_is_not_a_link_failure() {
  ElmPoll p = fresh(); takeVoltage(p, 0);                 // adapters without ATRV answer "?"
  for (int i = 0; i < 20; i++) step(p, ElmStatus::UNKNOWN_COMMAND, 10 + i);
  TEST_ASSERT_TRUE(pollMode(p) != PollMode::DOWN);
}

void test_link_lost_event_goes_down() {
  ElmPoll p = fresh(); takeVoltage(p, 0);
  ask(p, 5);                                              // a request is out when the link drops
  pollOnLinkLost(p, 10);
  TEST_ASSERT_EQUAL((int)PollMode::DOWN, (int)pollMode(p));
  PollLog l = answer(p, ElmStatus::OK, 11);               // the late result is ignored
  TEST_ASSERT_FALSE(l.log);
}

void test_reconnect_backoff_doubles_to_the_cap() {
  ElmPoll p = fresh();
  pollOnLinkLost(p, 1000);
  TEST_ASSERT_FALSE(pollReconnectDue(p, 1999));
  TEST_ASSERT_TRUE(pollReconnectDue(p, 2000));            // first retry after 1 s
  uint32_t now = 2000;
  const uint32_t waits[] = {2000, 4000, 8000, 16000, 30000, 30000};
  for (size_t i = 0; i < sizeof waits / sizeof waits[0]; i++) {
    pollOnReconnect(p, false, now);
    TEST_ASSERT_FALSE(pollReconnectDue(p, now + waits[i] - 1));
    TEST_ASSERT_TRUE(pollReconnectDue(p, now + waits[i]));
    now += waits[i];
  }
}

void test_reconnect_success_restarts_cleanly() {
  ElmPoll p = fresh(); takeVoltage(p, 0);
  ask(p, 1); answer(p, ElmStatus::NO_DATA, 1);            // RPM NO DATA logged
  pollOnLinkLost(p, 10);
  pollOnReconnect(p, false, 1010);                         // one failed attempt first
  pollOnReconnect(p, true, 3010);
  TEST_ASSERT_EQUAL((int)PollMode::NORMAL, (int)pollMode(p));
  PollRequest r = ask(p, 3011);
  TEST_ASSERT_EQUAL((int)PollKind::VOLTAGE, (int)r.kind); // voltage first again
  answer(p, ElmStatus::OK, 3011);
  r = ask(p, 3012);
  TEST_ASSERT_EQUAL_UINT8(PID_RPM, r.pid);
  PollLog l = answer(p, ElmStatus::NO_DATA, 3012);
  TEST_ASSERT_TRUE(l.log);                                // statuses forgotten, so NO DATA is logged again
  pollOnLinkLost(p, 4000);
  TEST_ASSERT_TRUE(pollReconnectDue(p, 5000));            // backoff started over at 1 s
}

void test_reconnect_only_counts_while_down() {
  ElmPoll p = fresh();
  TEST_ASSERT_FALSE(pollReconnectDue(p, 100000));
  pollOnReconnect(p, false, 5);
  TEST_ASSERT_EQUAL((int)PollMode::NORMAL, (int)pollMode(p));
}

// ---- pacing and clock wrap ----
void test_gap_between_requests() {
  ElmPoll p; pollInit(p, 50);
  step(p, ElmStatus::OK, 1000);
  PollRequest r;
  TEST_ASSERT_FALSE(pollNext(p, 1049, r));
  TEST_ASSERT_TRUE(pollNext(p, 1050, r));
}

void test_clock_wrap() {
  ElmPoll p; pollInit(p, 50);
  uint32_t t = 0xFFFFFF00u;
  step(p, ElmStatus::OK, t);                              // voltage
  PollRequest r;
  TEST_ASSERT_FALSE(pollNext(p, t + 10, r));              // gap not elapsed
  TEST_ASSERT_TRUE(pollNext(p, t + 60, r));               // wrapped past zero
  answer(p, ElmStatus::OK, t + 60);
  uint32_t later = t + POLL_VOLTAGE_EVERY_MS;             // wraps around 2^32
  TEST_ASSERT_TRUE(pollNext(p, later, r));
  TEST_ASSERT_EQUAL((int)PollKind::VOLTAGE, (int)r.kind);
}

void test_voltage_millivolts() {
  TEST_ASSERT_EQUAL_UINT32(12100, pollVoltageMv(12.1f));
  TEST_ASSERT_EQUAL_UINT32(11900, pollVoltageMv(11.9f));
  TEST_ASSERT_EQUAL_UINT32(0, pollVoltageMv(-1.0f));
  TEST_ASSERT_EQUAL_UINT32(65000, pollVoltageMv(1000.0f));
}

// ---- raw bytes from the parser (what gets logged) ----
void test_parse_raw_bytes() {
  uint8_t out[4] = {9, 9, 9, 9}; uint8_t n = 0;
  TEST_ASSERT_TRUE(elmParseMode01Raw("41 0C 1A F8\r\r>", PID_RPM, out, n) == ElmStatus::OK);
  TEST_ASSERT_EQUAL_UINT8(2, n);
  TEST_ASSERT_EQUAL_UINT8(0x1A, out[0]);
  TEST_ASSERT_EQUAL_UINT8(0xF8, out[1]);
  TEST_ASSERT_TRUE(elmParseMode01Raw("41 0D 3C\r\r>", PID_SPEED_KMH, out, n) == ElmStatus::OK);
  TEST_ASSERT_EQUAL_UINT8(1, n);
  TEST_ASSERT_EQUAL_UINT8(0x3C, out[0]);
}

void test_parse_raw_failure_leaves_outputs_alone() {
  uint8_t out[4] = {9, 9, 9, 9}; uint8_t n = 77;
  TEST_ASSERT_TRUE(elmParseMode01Raw("NO DATA\r\r>", PID_RPM, out, n) == ElmStatus::NO_DATA);
  TEST_ASSERT_EQUAL_UINT8(77, n);
  TEST_ASSERT_EQUAL_UINT8(9, out[0]);
  TEST_ASSERT_TRUE(elmParseMode01Raw("41 0D 3C\r\r>", PID_RPM, out, n) == ElmStatus::WRONG_PID);
  TEST_ASSERT_TRUE(elmParseMode01Raw("41 99 3C\r\r>", 0x99, out, n) == ElmStatus::UNSUPPORTED_PID);
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_first_request_is_voltage);
  RUN_TEST(test_fast_group_then_one_slow_pid);
  RUN_TEST(test_no_new_request_while_one_is_out);
  RUN_TEST(test_unrequested_result_is_ignored);
  RUN_TEST(test_voltage_every_10_seconds);
  RUN_TEST(test_voltage_does_not_skip_the_fast_group);
  RUN_TEST(test_ok_readings_always_logged);
  RUN_TEST(test_no_data_logged_once_per_pid);
  RUN_TEST(test_ok_after_no_data_is_logged);
  RUN_TEST(test_error_then_no_data_both_logged);
  RUN_TEST(test_different_failure_kinds_map_to_error);
  RUN_TEST(test_codes_match_the_log_format);
  RUN_TEST(test_unable_to_connect_means_key_off_not_a_lost_adapter);
  RUN_TEST(test_unable_to_connect_is_logged_once_as_no_data);
  RUN_TEST(test_four_no_data_is_not_key_off);
  RUN_TEST(test_five_no_data_in_a_row_is_key_off);
  RUN_TEST(test_an_ok_reading_breaks_the_no_data_run);
  RUN_TEST(test_voltage_does_not_count_toward_key_off);
  RUN_TEST(test_key_off_asks_only_voltage_and_rpm_probe);
  RUN_TEST(test_key_off_voltage_every_5_seconds);
  RUN_TEST(test_key_off_probe_no_data_not_relogged);
  RUN_TEST(test_key_on_again_returns_to_normal_and_restarts_cycle);
  RUN_TEST(test_six_unusable_replies_take_the_link_down);
  RUN_TEST(test_good_reply_resets_the_failure_count);
  RUN_TEST(test_no_data_proves_the_adapter_is_alive);
  RUN_TEST(test_question_mark_is_not_a_link_failure);
  RUN_TEST(test_link_lost_event_goes_down);
  RUN_TEST(test_reconnect_backoff_doubles_to_the_cap);
  RUN_TEST(test_reconnect_success_restarts_cleanly);
  RUN_TEST(test_reconnect_only_counts_while_down);
  RUN_TEST(test_gap_between_requests);
  RUN_TEST(test_clock_wrap);
  RUN_TEST(test_voltage_millivolts);
  RUN_TEST(test_parse_raw_bytes);
  RUN_TEST(test_parse_raw_failure_leaves_outputs_alone);
  return UNITY_END();
}
