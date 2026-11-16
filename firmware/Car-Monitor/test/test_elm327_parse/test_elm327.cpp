#include <unity.h>
#include <string.h>
#include "elm_parse.h"

void setUp() {}
void tearDown() {}

static const float SENTINEL = -12345.0f;

// ---- decoding good replies ----
void test_rpm() {
  float v = SENTINEL;
  TEST_ASSERT_TRUE(elmParseMode01("41 0C 1A F8\r\r>", PID_RPM, v) == ElmStatus::OK);
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 1726.0f, v);              // 0x1AF8 / 4
}
void test_rpm_idle_and_zero() {
  float v = SENTINEL;
  TEST_ASSERT_TRUE(elmParseMode01("41 0C 0B B8\r\r>", PID_RPM, v) == ElmStatus::OK);
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 750.0f, v);
  TEST_ASSERT_TRUE(elmParseMode01("41 0C 00 00\r\r>", PID_RPM, v) == ElmStatus::OK);
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 0.0f, v);                 // a real zero is OK, unlike a failed read
}
void test_speed() {
  float v = SENTINEL;
  TEST_ASSERT_TRUE(elmParseMode01("41 0D 3C\r\r>", PID_SPEED_KMH, v) == ElmStatus::OK);
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 60.0f, v);
}
void test_coolant_and_intake_offset() {
  float v = SENTINEL;
  TEST_ASSERT_TRUE(elmParseMode01("41 05 7B\r\r>", PID_COOLANT_C, v) == ElmStatus::OK);
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 83.0f, v);                // 0x7B - 40
  TEST_ASSERT_TRUE(elmParseMode01("41 0F 3C\r\r>", PID_INTAKE_C, v) == ElmStatus::OK);
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 20.0f, v);
  TEST_ASSERT_TRUE(elmParseMode01("41 05 00\r\r>", PID_COOLANT_C, v) == ElmStatus::OK);
  TEST_ASSERT_FLOAT_WITHIN(0.01f, -40.0f, v);               // can be below zero
}
void test_throttle_percent() {
  float v = SENTINEL;
  TEST_ASSERT_TRUE(elmParseMode01("41 11 80\r\r>", PID_THROTTLE_PCT, v) == ElmStatus::OK);
  TEST_ASSERT_FLOAT_WITHIN(0.05f, 50.2f, v);
  TEST_ASSERT_TRUE(elmParseMode01("41 11 FF\r\r>", PID_THROTTLE_PCT, v) == ElmStatus::OK);
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 100.0f, v);
}

// ---- tolerating how real adapters talk ----
void test_no_spaces_lowercase_and_no_prompt() {
  float v = SENTINEL;
  TEST_ASSERT_TRUE(elmParseMode01("410c1af8", PID_RPM, v) == ElmStatus::OK);
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 1726.0f, v);
  v = SENTINEL;
  TEST_ASSERT_TRUE(elmParseMode01("41 0c 1a f8\r\r>", PID_RPM, v) == ElmStatus::OK);
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 1726.0f, v);
}
void test_echo_and_linefeeds() {
  float v = SENTINEL;
  TEST_ASSERT_TRUE(elmParseMode01("010C\r\n41 0C 1A F8\r\n\r\n>", PID_RPM, v) == ElmStatus::OK);
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 1726.0f, v);
}
void test_searching_prefix() {
  float v = SENTINEL;
  TEST_ASSERT_TRUE(elmParseMode01("SEARCHING...\r41 0D 00\r\r>", PID_SPEED_KMH, v) == ElmStatus::OK);
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 0.0f, v);
  v = SENTINEL;
  TEST_ASSERT_TRUE(elmParseMode01("SEARCHING...41 0D 14\r\r>", PID_SPEED_KMH, v) == ElmStatus::OK);   // glued
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 20.0f, v);
}
void test_first_of_several_ecus_wins() {
  float v = SENTINEL;
  TEST_ASSERT_TRUE(elmParseMode01("41 0C 1A F8\r41 0C 00 00\r\r>", PID_RPM, v) == ElmStatus::OK);
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 1726.0f, v);
}
void test_extra_trailing_bytes_are_ignored() {
  float v = SENTINEL;
  TEST_ASSERT_TRUE(elmParseMode01("41 0D 3C 00\r\r>", PID_SPEED_KMH, v) == ElmStatus::OK);
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 60.0f, v);
}

// ---- failures must never produce a value ----
static void assertFails(const char* raw, uint8_t pid, ElmStatus expect) {
  float v = SENTINEL;
  ElmStatus s = elmParseMode01(raw, pid, v);
  TEST_ASSERT_TRUE(s == expect);
  TEST_ASSERT_FLOAT_WITHIN(0.0001f, SENTINEL, v);           // untouched
}
void test_no_data() {
  assertFails("NO DATA\r\r>", PID_RPM, ElmStatus::NO_DATA);
  assertFails("SEARCHING...\rNO DATA\r\r>", PID_RPM, ElmStatus::NO_DATA);
}
void test_unable_to_connect_and_bus_errors() {
  assertFails("SEARCHING...\rUNABLE TO CONNECT\r\r>", PID_RPM, ElmStatus::UNABLE_TO_CONNECT);
  assertFails("BUS INIT: ...ERROR\r\r>", PID_RPM, ElmStatus::BUS_ERROR);
  assertFails("CAN ERROR\r\r>", PID_RPM, ElmStatus::BUS_ERROR);
  assertFails("BUS BUSY\r\r>", PID_RPM, ElmStatus::BUS_ERROR);
  assertFails("BUFFER FULL\r\r>", PID_RPM, ElmStatus::BUS_ERROR);
}
void test_unknown_command_and_negative_response() {
  assertFails("?\r\r>", PID_RPM, ElmStatus::UNKNOWN_COMMAND);
  assertFails("7F 01 12\r\r>", PID_RPM, ElmStatus::NEGATIVE_RESPONSE);
}
void test_wrong_pid_is_rejected() {
  assertFails("41 0D 3C\r\r>", PID_RPM, ElmStatus::WRONG_PID);   // a stale speed reply to an rpm request
}
void test_truncated_and_garbage() {
  assertFails("41 0C 1A\r\r>", PID_RPM, ElmStatus::BAD_FORMAT);  // one data byte short
  assertFails("41 0C\r\r>", PID_RPM, ElmStatus::BAD_FORMAT);
  assertFails("ZZZ\r\r>", PID_RPM, ElmStatus::BAD_FORMAT);
  assertFails("41 0C 1A F\r\r>", PID_RPM, ElmStatus::BAD_FORMAT); // odd number of digits
  assertFails("", PID_RPM, ElmStatus::BAD_FORMAT);
  assertFails(">", PID_RPM, ElmStatus::BAD_FORMAT);
  assertFails("\r\r\r", PID_RPM, ElmStatus::BAD_FORMAT);
}
void test_null_and_unknown_pid() {
  float v = SENTINEL;
  TEST_ASSERT_TRUE(elmParseMode01(nullptr, PID_RPM, v) == ElmStatus::BAD_FORMAT);
  TEST_ASSERT_TRUE(elmParseMode01("41 99 00", 0x99, v) == ElmStatus::UNSUPPORTED_PID);
  TEST_ASSERT_FLOAT_WITHIN(0.0001f, SENTINEL, v);
}
void test_data_beats_error_in_same_reply() {
  float v = SENTINEL;                                       // one ECU answers, another says nothing useful
  TEST_ASSERT_TRUE(elmParseMode01("NO DATA\r41 0D 3C\r\r>", PID_SPEED_KMH, v) == ElmStatus::OK);
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 60.0f, v);
}
void test_very_long_line_does_not_overflow() {
  char big[400];
  memset(big, 'A', sizeof big - 1);
  big[sizeof big - 1] = '\0';
  assertFails(big, PID_RPM, ElmStatus::BAD_FORMAT);
}
void test_status_names() {
  TEST_ASSERT_TRUE(strcmp(elmStatusName(ElmStatus::NO_DATA), "NO DATA") == 0);
  TEST_ASSERT_TRUE(strcmp(elmStatusName(ElmStatus::OK), "OK") == 0);
}

// ---- reply assembly (BLE delivers 20-byte pieces) ----
void test_reply_in_pieces() {
  ElmReplyBuffer b;
  TEST_ASSERT_FALSE(elmFeed(b, "41 0C ", 6));
  TEST_ASSERT_FALSE(elmFeed(b, "1A F8\r", 6));
  TEST_ASSERT_FALSE(elmFeed(b, "\r", 1));
  TEST_ASSERT_TRUE(elmFeed(b, ">", 1));
  float v = SENTINEL;
  TEST_ASSERT_TRUE(elmParseMode01(elmReplyText(b), PID_RPM, v) == ElmStatus::OK);
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 1726.0f, v);
}
void test_prompt_inside_chunk_and_trailing_bytes_ignored() {
  ElmReplyBuffer b;
  TEST_ASSERT_TRUE(elmFeed(b, "41 0D 3C\r\r>JUNK", 15));
  TEST_ASSERT_TRUE(strstr(elmReplyText(b), "JUNK") == nullptr);
  TEST_ASSERT_TRUE(elmFeed(b, "MORE>", 5));                  // ignored until reset
  TEST_ASSERT_TRUE(strstr(elmReplyText(b), "MORE") == nullptr);
}
void test_reset_allows_next_reply() {
  ElmReplyBuffer b;
  elmFeed(b, "NO DATA\r\r>", 10);
  elmReplyReset(b);
  TEST_ASSERT_FALSE(b.complete);
  TEST_ASSERT_EQUAL(0, (long)b.len);
  TEST_ASSERT_TRUE(elmFeed(b, "41 0D 14\r\r>", 11));
  float v = SENTINEL;
  TEST_ASSERT_TRUE(elmParseMode01(elmReplyText(b), PID_SPEED_KMH, v) == ElmStatus::OK);
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 20.0f, v);
}
void test_overflow_is_flagged_and_safe() {
  ElmReplyBuffer b;
  char chunk[40];
  memset(chunk, 'A', sizeof chunk);
  for (int i = 0; i < 20; i++) elmFeed(b, chunk, sizeof chunk);   // 800 bytes, never a prompt
  TEST_ASSERT_TRUE(b.overflow);
  TEST_ASSERT_FALSE(b.complete);
  TEST_ASSERT_TRUE(elmFeed(b, ">", 1));
  float v = SENTINEL;
  TEST_ASSERT_TRUE(elmParseMode01(elmReplyText(b), PID_RPM, v) == ElmStatus::BAD_FORMAT);
  TEST_ASSERT_TRUE(b.len < ELM_REPLY_MAX);
}
void test_null_chunk() {
  ElmReplyBuffer b;
  TEST_ASSERT_FALSE(elmFeed(b, nullptr, 5));
  TEST_ASSERT_EQUAL(0, (long)b.len);
}

// ---- start-up sequence ----
void test_init_happy_path() {
  ElmInit s;
  const char* expect[] = {"ATZ", "ATE0", "ATL0", "ATS0", "ATH0", "ATSP0"};
  const char* reply[]  = {"ATZ\r\rELM327 v1.5\r\r>", "ATE0\rOK\r\r>", "OK\r\r>", "OK\r\r>", "OK\r\r>", "OK\r\r>"};
  for (int i = 0; i < 6; i++) {
    TEST_ASSERT_FALSE(elmInitDone(s));
    TEST_ASSERT_TRUE(strcmp(elmInitCommand(s), expect[i]) == 0);
    TEST_ASSERT_TRUE(elmInitOnReply(s, reply[i]));
  }
  TEST_ASSERT_TRUE(elmInitDone(s));
  TEST_ASSERT_FALSE(elmInitFailed(s));
  TEST_ASSERT_TRUE(elmInitCommand(s) == nullptr);
}
void test_init_retries_then_gives_up() {
  ElmInit s;
  TEST_ASSERT_FALSE(elmInitOnReply(s, "?\r\r>"));
  TEST_ASSERT_TRUE(strcmp(elmInitCommand(s), "ATZ") == 0);      // same command again
  TEST_ASSERT_FALSE(elmInitOnReply(s, "garbage"));
  TEST_ASSERT_FALSE(elmInitFailed(s));
  TEST_ASSERT_FALSE(elmInitOnReply(s, ""));
  TEST_ASSERT_TRUE(elmInitFailed(s));
  TEST_ASSERT_TRUE(elmInitCommand(s) == nullptr);
  TEST_ASSERT_FALSE(elmInitOnReply(s, "ELM327 v1.5"));          // ignored once failed
}
void test_init_success_resets_the_failure_count() {
  ElmInit s;
  elmInitOnReply(s, "?");
  elmInitOnReply(s, "?");
  TEST_ASSERT_TRUE(elmInitOnReply(s, "ELM327 v2.1"));
  TEST_ASSERT_EQUAL(0, (long)s.fails);
  elmInitOnReply(s, "?");
  elmInitOnReply(s, "?");
  TEST_ASSERT_FALSE(elmInitFailed(s));                          // two fails on ATE0, not three in a row overall
}
void test_init_wrong_reply_for_step() {
  ElmInit s;
  TEST_ASSERT_FALSE(elmInitOnReply(s, "OK\r\r>"));              // ATZ must answer with the version, not OK
  TEST_ASSERT_TRUE(s.step == 0);
  TEST_ASSERT_FALSE(elmInitOnReply(s, nullptr));
}

// ---- more PIDs ----
void test_engine_load_and_map_and_timing() {
  float v = SENTINEL;
  TEST_ASSERT_TRUE(elmParseMode01("41 04 80\r\r>", PID_ENGINE_LOAD_PCT, v) == ElmStatus::OK);
  TEST_ASSERT_FLOAT_WITHIN(0.05f, 50.2f, v);
  TEST_ASSERT_TRUE(elmParseMode01("41 0B 63\r\r>", PID_MAP_KPA, v) == ElmStatus::OK);
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 99.0f, v);                // about atmospheric pressure, engine off
  TEST_ASSERT_TRUE(elmParseMode01("41 0E 90\r\r>", PID_TIMING_ADV_DEG, v) == ElmStatus::OK);
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 8.0f, v);                 // 0x90 / 2 - 64
  TEST_ASSERT_TRUE(elmParseMode01("41 0E 00\r\r>", PID_TIMING_ADV_DEG, v) == ElmStatus::OK);
  TEST_ASSERT_FLOAT_WITHIN(0.01f, -64.0f, v);
}
void test_fuel_trims_are_signed() {
  float v = SENTINEL;
  TEST_ASSERT_TRUE(elmParseMode01("41 06 80\r\r>", PID_FUEL_TRIM_ST_PCT, v) == ElmStatus::OK);
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 0.0f, v);                 // 128 = no correction
  TEST_ASSERT_TRUE(elmParseMode01("41 06 00\r\r>", PID_FUEL_TRIM_ST_PCT, v) == ElmStatus::OK);
  TEST_ASSERT_FLOAT_WITHIN(0.01f, -100.0f, v);
  TEST_ASSERT_TRUE(elmParseMode01("41 07 FF\r\r>", PID_FUEL_TRIM_LT_PCT, v) == ElmStatus::OK);
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 99.22f, v);
  TEST_ASSERT_TRUE(elmParseMode01("41 07 8C\r\r>", PID_FUEL_TRIM_LT_PCT, v) == ElmStatus::OK);
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 9.375f, v);
}
void test_maf_and_ecu_voltage_two_bytes() {
  float v = SENTINEL;
  TEST_ASSERT_TRUE(elmParseMode01("41 10 01 F4\r\r>", PID_MAF_GPS, v) == ElmStatus::OK);
  TEST_ASSERT_FLOAT_WITHIN(0.001f, 5.0f, v);                // 500 / 100 g/s
  TEST_ASSERT_TRUE(elmParseMode01("41 42 36 B0\r\r>", PID_CTRL_VOLTAGE, v) == ElmStatus::OK);
  TEST_ASSERT_FLOAT_WITHIN(0.001f, 14.0f, v);               // 14000 / 1000 V
  float w = SENTINEL;
  TEST_ASSERT_TRUE(elmParseMode01("41 10 01\r\r>", PID_MAF_GPS, w) == ElmStatus::BAD_FORMAT);   // one byte short
  TEST_ASSERT_FLOAT_WITHIN(0.0001f, SENTINEL, w);
}
void test_request_text() {
  char r[8];
  TEST_ASSERT_TRUE(elmMode01Request(PID_RPM, r, sizeof r));
  TEST_ASSERT_TRUE(strcmp(r, "010C") == 0);
  TEST_ASSERT_TRUE(elmMode01Request(0x00, r, sizeof r));
  TEST_ASSERT_TRUE(strcmp(r, "0100") == 0);
  TEST_ASSERT_TRUE(elmMode01Request(0x42, r, sizeof r));
  TEST_ASSERT_TRUE(strcmp(r, "0142") == 0);
  TEST_ASSERT_FALSE(elmMode01Request(0x0C, r, 4));          // no room for the terminator
  TEST_ASSERT_FALSE(elmMode01Request(0x0C, nullptr, 8));
}

// ---- which PIDs does the car answer ----
void test_supported_mask() {
  uint32_t m = 0;
  TEST_ASSERT_TRUE(elmParseSupported("41 00 BE 1F A8 13\r\r>", 0x00, m) == ElmStatus::OK);
  TEST_ASSERT_EQUAL_UINT32(0xBE1FA813u, m);
  TEST_ASSERT_TRUE(elmPidSupported(m, 0x00, 0x01));         // first bit
  TEST_ASSERT_FALSE(elmPidSupported(m, 0x00, 0x02));        // 0xBE = 1011 1110: PID 02 is clear
  TEST_ASSERT_TRUE(elmPidSupported(m, 0x00, 0x03));
  TEST_ASSERT_TRUE(elmPidSupported(m, 0x00, 0x0C));         // RPM
  TEST_ASSERT_TRUE(elmPidSupported(m, 0x00, 0x0D));         // speed
  TEST_ASSERT_TRUE(elmPidSupported(m, 0x00, 0x20));         // last bit set: ask the next block
  TEST_ASSERT_FALSE(elmPidSupported(m, 0x00, 0x00));        // outside the block
  TEST_ASSERT_FALSE(elmPidSupported(m, 0x00, 0x21));
}
void test_supported_second_block() {
  uint32_t m = 0;
  TEST_ASSERT_TRUE(elmParseSupported("41 20 80 00 00 01\r\r>", 0x20, m) == ElmStatus::OK);
  TEST_ASSERT_TRUE(elmPidSupported(m, 0x20, 0x21));
  TEST_ASSERT_FALSE(elmPidSupported(m, 0x20, 0x22));
  TEST_ASSERT_TRUE(elmPidSupported(m, 0x20, 0x40));
  TEST_ASSERT_FALSE(elmPidSupported(m, 0x20, 0x20));
}
void test_supported_failures() {
  uint32_t m = 0xDEADBEEFu;
  TEST_ASSERT_TRUE(elmParseSupported("NO DATA\r\r>", 0x00, m) == ElmStatus::NO_DATA);
  TEST_ASSERT_TRUE(elmParseSupported("41 00 BE 1F\r\r>", 0x00, m) == ElmStatus::BAD_FORMAT);   // truncated
  TEST_ASSERT_TRUE(elmParseSupported("41 20 80 00 00 01\r\r>", 0x00, m) == ElmStatus::WRONG_PID);
  TEST_ASSERT_TRUE(elmParseSupported("41 0C 1A F8\r\r>", 0x05, m) == ElmStatus::UNSUPPORTED_PID);   // not a block start
  TEST_ASSERT_EQUAL_UINT32(0xDEADBEEFu, m);                 // untouched
}

// ---- battery voltage (ATRV) ----
void test_voltage_forms() {
  float v = SENTINEL;
  TEST_ASSERT_TRUE(elmParseVoltage("12.6V\r\r>", v) == ElmStatus::OK);
  TEST_ASSERT_FLOAT_WITHIN(0.001f, 12.6f, v);
  TEST_ASSERT_TRUE(elmParseVoltage("14.1\r\r>", v) == ElmStatus::OK);            // no V
  TEST_ASSERT_FLOAT_WITHIN(0.001f, 14.1f, v);
  TEST_ASSERT_TRUE(elmParseVoltage(" 11.8 V \r\n>", v) == ElmStatus::OK);        // spaces
  TEST_ASSERT_FLOAT_WITHIN(0.001f, 11.8f, v);
  TEST_ASSERT_TRUE(elmParseVoltage("ATRV\r12.3V\r\r>", v) == ElmStatus::OK);    // echo on
  TEST_ASSERT_FLOAT_WITHIN(0.001f, 12.3f, v);
  TEST_ASSERT_TRUE(elmParseVoltage("12v\r>", v) == ElmStatus::OK);                // whole number, lower case
  TEST_ASSERT_FLOAT_WITHIN(0.001f, 12.0f, v);
}
void test_voltage_failures() {
  float v = SENTINEL;
  TEST_ASSERT_TRUE(elmParseVoltage("?\r\r>", v) == ElmStatus::UNKNOWN_COMMAND);  // adapter has no ATRV
  TEST_ASSERT_TRUE(elmParseVoltage("0.0V\r\r>", v) == ElmStatus::BAD_FORMAT);    // clone without voltage sense
  TEST_ASSERT_TRUE(elmParseVoltage("99.9V\r\r>", v) == ElmStatus::BAD_FORMAT);   // not a battery
  TEST_ASSERT_TRUE(elmParseVoltage("12.6X\r\r>", v) == ElmStatus::BAD_FORMAT);
  TEST_ASSERT_TRUE(elmParseVoltage("V\r\r>", v) == ElmStatus::BAD_FORMAT);
  TEST_ASSERT_TRUE(elmParseVoltage("", v) == ElmStatus::BAD_FORMAT);
  TEST_ASSERT_TRUE(elmParseVoltage(nullptr, v) == ElmStatus::BAD_FORMAT);
  TEST_ASSERT_FLOAT_WITHIN(0.0001f, SENTINEL, v);                                   // untouched every time
  TEST_ASSERT_TRUE(strcmp(ELM_CMD_VOLTAGE, "ATRV") == 0);
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_rpm);
  RUN_TEST(test_rpm_idle_and_zero);
  RUN_TEST(test_speed);
  RUN_TEST(test_coolant_and_intake_offset);
  RUN_TEST(test_throttle_percent);
  RUN_TEST(test_no_spaces_lowercase_and_no_prompt);
  RUN_TEST(test_echo_and_linefeeds);
  RUN_TEST(test_searching_prefix);
  RUN_TEST(test_first_of_several_ecus_wins);
  RUN_TEST(test_extra_trailing_bytes_are_ignored);
  RUN_TEST(test_no_data);
  RUN_TEST(test_unable_to_connect_and_bus_errors);
  RUN_TEST(test_unknown_command_and_negative_response);
  RUN_TEST(test_wrong_pid_is_rejected);
  RUN_TEST(test_truncated_and_garbage);
  RUN_TEST(test_null_and_unknown_pid);
  RUN_TEST(test_data_beats_error_in_same_reply);
  RUN_TEST(test_very_long_line_does_not_overflow);
  RUN_TEST(test_status_names);
  RUN_TEST(test_reply_in_pieces);
  RUN_TEST(test_prompt_inside_chunk_and_trailing_bytes_ignored);
  RUN_TEST(test_reset_allows_next_reply);
  RUN_TEST(test_overflow_is_flagged_and_safe);
  RUN_TEST(test_null_chunk);
  RUN_TEST(test_init_happy_path);
  RUN_TEST(test_init_retries_then_gives_up);
  RUN_TEST(test_init_success_resets_the_failure_count);
  RUN_TEST(test_init_wrong_reply_for_step);
  RUN_TEST(test_engine_load_and_map_and_timing);
  RUN_TEST(test_fuel_trims_are_signed);
  RUN_TEST(test_maf_and_ecu_voltage_two_bytes);
  RUN_TEST(test_request_text);
  RUN_TEST(test_supported_mask);
  RUN_TEST(test_supported_second_block);
  RUN_TEST(test_supported_failures);
  RUN_TEST(test_voltage_forms);
  RUN_TEST(test_voltage_failures);
  return UNITY_END();
}
