#include <unity.h>
#include "gps_status.h"

void test_never_heard_is_link_lost() {
  GpsTiming t;
  TEST_ASSERT_EQUAL(GpsStatus::LINK_LOST, gpsStatus(5000, t));
}
void test_fresh_fix_is_ok() {
  GpsTiming t{9000, 9000, true, true};
  TEST_ASSERT_EQUAL(GpsStatus::OK, gpsStatus(10000, t));
}
void test_talking_but_old_fix_is_no_fix() {
  GpsTiming t{9900, 5000, true, true};
  TEST_ASSERT_EQUAL(GpsStatus::NO_FIX, gpsStatus(10000, t));
}
void test_silent_module_is_link_lost() {
  GpsTiming t{5000, 5000, true, true};
  TEST_ASSERT_EQUAL(GpsStatus::LINK_LOST, gpsStatus(10000, t));
}
void test_millis_wraparound() {
  GpsTiming t{0xFFFFFF00u, 0xFFFFFF00u, true, true};
  TEST_ASSERT_EQUAL(GpsStatus::OK, gpsStatus(100, t));   // clock wrapped, only ~356 ms passed
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_never_heard_is_link_lost);
  RUN_TEST(test_fresh_fix_is_ok);
  RUN_TEST(test_talking_but_old_fix_is_no_fix);
  RUN_TEST(test_silent_module_is_link_lost);
  RUN_TEST(test_millis_wraparound);
  return UNITY_END();
}
