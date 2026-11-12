#include <unity.h>
#include "movetrigger.h"

void setUp() {}
void tearDown() {}

void test_needs_consecutive_records() {
  MoveTrigger t;
  for (int i = 0; i < START_CONSECUTIVE - 1; i++) TEST_ASSERT_FALSE(triggerOnRecord(t, true, 20.0f));
  TEST_ASSERT_TRUE(triggerOnRecord(t, true, 20.0f));       // the 5th in a row fires
}
void test_exactly_threshold_is_not_moving() {
  MoveTrigger t;
  for (int i = 0; i < 20; i++) TEST_ASSERT_FALSE(triggerOnRecord(t, true, 5.0f));   // "over 5 mph", not 5.0
  for (int i = 0; i < START_CONSECUTIVE - 1; i++) triggerOnRecord(t, true, 5.1f);
  TEST_ASSERT_TRUE(triggerOnRecord(t, true, 5.1f));          // just above does count
}
void test_single_spike_does_not_fire() {
  MoveTrigger t;                                            // the 4.6 mph drift spikes you saw, and worse
  TEST_ASSERT_FALSE(triggerOnRecord(t, true, 9.0f));
  TEST_ASSERT_FALSE(triggerOnRecord(t, true, 0.0f));
  for (int i = 0; i < 3; i++) TEST_ASSERT_FALSE(triggerOnRecord(t, true, 9.0f));
  TEST_ASSERT_EQUAL_UINT8(3, t.streak);
}
void test_slow_record_restarts_the_count() {
  MoveTrigger t;
  for (int i = 0; i < 4; i++) triggerOnRecord(t, true, 30.0f);
  TEST_ASSERT_FALSE(triggerOnRecord(t, true, 3.0f));
  TEST_ASSERT_EQUAL_UINT8(0, t.streak);
}
void test_no_fix_never_fires_even_with_speed() {
  MoveTrigger t;
  for (int i = 0; i < 50; i++) TEST_ASSERT_FALSE(triggerOnRecord(t, false, 60.0f));
}
void test_keeps_returning_true_while_moving() {
  MoveTrigger t;
  for (int i = 0; i < START_CONSECUTIVE; i++) triggerOnRecord(t, true, 40.0f);
  TEST_ASSERT_TRUE(triggerOnRecord(t, true, 40.0f));
  TEST_ASSERT_TRUE(triggerOnRecord(t, true, 40.0f));
}
void test_streak_does_not_overflow() {
  MoveTrigger t;
  for (int i = 0; i < 1000; i++) triggerOnRecord(t, true, 40.0f);
  TEST_ASSERT_EQUAL_UINT8(255, t.streak);
  TEST_ASSERT_FALSE(triggerOnRecord(t, false, 40.0f));
  TEST_ASSERT_EQUAL_UINT8(0, t.streak);
}
void test_reset_and_custom_settings() {
  MoveTrigger t;
  for (int i = 0; i < 4; i++) triggerOnRecord(t, true, 30.0f);
  triggerReset(t);
  TEST_ASSERT_EQUAL_UINT8(0, t.streak);
  TEST_ASSERT_TRUE(triggerOnRecord(t, true, 1.0f, 0.5f, 1));   // bench setting: tiny threshold, one record
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_needs_consecutive_records);
  RUN_TEST(test_exactly_threshold_is_not_moving);
  RUN_TEST(test_single_spike_does_not_fire);
  RUN_TEST(test_slow_record_restarts_the_count);
  RUN_TEST(test_no_fix_never_fires_even_with_speed);
  RUN_TEST(test_keeps_returning_true_while_moving);
  RUN_TEST(test_streak_does_not_overflow);
  RUN_TEST(test_reset_and_custom_settings);
  return UNITY_END();
}
