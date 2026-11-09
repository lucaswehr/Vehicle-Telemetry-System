#include <unity.h>
#include "sdstate.h"

void test_starts_unmounted_and_retries_immediately() {
  SdHealth h;
  TEST_ASSERT_TRUE(h.state == SdState::UNMOUNTED);
  TEST_ASSERT_TRUE(sdShouldRetry(h, 1000));
}
void test_mount_ok_goes_to_logging() {
  SdHealth h;
  sdOnMountResult(h, true, 100);
  TEST_ASSERT_TRUE(h.state == SdState::LOGGING);
  TEST_ASSERT_EQUAL_UINT32(0, h.remounts);          // first mount is not a "re"mount
  TEST_ASSERT_FALSE(sdShouldRetry(h, 99999));       // never retry while logging
}
void test_mount_failure_backs_off_and_caps() {
  SdHealth h;
  uint32_t t = 0;
  uint32_t expect[] = {2000, 4000, 8000, 10000, 10000};
  for (int i = 0; i < 5; i++) {
    sdOnMountResult(h, false, t);
    TEST_ASSERT_EQUAL_UINT32(expect[i], h.retryDelayMs);
    t += h.retryDelayMs;
  }
  TEST_ASSERT_EQUAL_UINT32(5, h.mountFailures);
}
void test_retry_waits_for_delay() {
  SdHealth h;
  sdOnMountResult(h, false, 5000);                  // delay now 2000
  TEST_ASSERT_FALSE(sdShouldRetry(h, 6999));
  TEST_ASSERT_TRUE(sdShouldRetry(h, 7000));
}
void test_retry_survives_millis_wraparound() {
  SdHealth h;
  sdOnMountResult(h, false, 0xFFFFFF00u);           // delay 2000
  TEST_ASSERT_FALSE(sdShouldRetry(h, 0x00000100u)); // only 512 ms later
  TEST_ASSERT_TRUE(sdShouldRetry(h, 0x000007D0u));  // > 2000 ms later, after the wrap
}
void test_single_write_failure_is_tolerated() {
  SdHealth h;
  sdOnMountResult(h, true, 0);
  TEST_ASSERT_FALSE(sdOnWriteResult(h, false, 10));
  TEST_ASSERT_TRUE(h.state == SdState::LOGGING);
  TEST_ASSERT_FALSE(sdOnWriteResult(h, true, 20));  // success resets the counter
  TEST_ASSERT_EQUAL_UINT8(0, h.consecutiveWriteFails);
}
void test_three_consecutive_failures_drop_to_unmounted() {
  SdHealth h;
  sdOnMountResult(h, true, 0);
  TEST_ASSERT_FALSE(sdOnWriteResult(h, false, 10));
  TEST_ASSERT_FALSE(sdOnWriteResult(h, false, 20));
  TEST_ASSERT_TRUE(sdOnWriteResult(h, false, 30));  // caller must close + SD.end()
  TEST_ASSERT_TRUE(h.state == SdState::UNMOUNTED);
  TEST_ASSERT_FALSE(sdShouldRetry(h, 500));
  TEST_ASSERT_TRUE(sdShouldRetry(h, 1030));
}
void test_remount_is_counted() {
  SdHealth h;
  sdOnMountResult(h, true, 0);
  for (int i = 0; i < 3; i++) sdOnWriteResult(h, false, 10);
  sdOnMountResult(h, true, 2000);
  TEST_ASSERT_TRUE(h.state == SdState::LOGGING);
  TEST_ASSERT_EQUAL_UINT32(1, h.remounts);
  TEST_ASSERT_EQUAL_UINT8(0, h.consecutiveWriteFails);
}
void test_write_result_ignored_when_unmounted() {
  SdHealth h;
  TEST_ASSERT_FALSE(sdOnWriteResult(h, false, 0));
  TEST_ASSERT_EQUAL_UINT8(0, h.consecutiveWriteFails);
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_starts_unmounted_and_retries_immediately);
  RUN_TEST(test_mount_ok_goes_to_logging);
  RUN_TEST(test_mount_failure_backs_off_and_caps);
  RUN_TEST(test_retry_waits_for_delay);
  RUN_TEST(test_retry_survives_millis_wraparound);
  RUN_TEST(test_single_write_failure_is_tolerated);
  RUN_TEST(test_three_consecutive_failures_drop_to_unmounted);
  RUN_TEST(test_remount_is_counted);
  RUN_TEST(test_write_result_ignored_when_unmounted);
  return UNITY_END();
}
