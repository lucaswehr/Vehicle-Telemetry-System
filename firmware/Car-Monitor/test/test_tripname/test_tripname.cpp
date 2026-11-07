#include <unity.h>
#include <string.h>
#include "tripname.h"

void test_name_format() {
  char b[40];
  TEST_ASSERT_TRUE(tripName(b, sizeof b, 71126, 3));      // 07 Nov 2026
  TEST_ASSERT_EQUAL_STRING("/2026-11-07_trip3.bin", b);
}
void test_name_end_of_year() {
  char b[40];
  TEST_ASSERT_TRUE(tripName(b, sizeof b, 311226, 12));
  TEST_ASSERT_EQUAL_STRING("/2026-12-31_trip12.bin", b);
}
void test_rejects_missing_date() {
  char b[40];
  TEST_ASSERT_FALSE(tripName(b, sizeof b, 0, 1));
  TEST_ASSERT_FALSE(tripDateValid(0));
}
void test_rejects_nonsense_date() {
  TEST_ASSERT_FALSE(tripDateValid(321326));   // day 32, month 13
  TEST_ASSERT_FALSE(tripDateValid(61080));    // year 2080: implausible
}
void test_rejects_small_buffer() {
  char b[8];
  TEST_ASSERT_FALSE(tripName(b, sizeof b, 71126, 1));
}
void test_rejects_bad_trip_number() {
  char b[40];
  TEST_ASSERT_FALSE(tripName(b, sizeof b, 71126, 0));
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_name_format);
  RUN_TEST(test_name_end_of_year);
  RUN_TEST(test_rejects_missing_date);
  RUN_TEST(test_rejects_nonsense_date);
  RUN_TEST(test_rejects_small_buffer);
  RUN_TEST(test_rejects_bad_trip_number);
  return UNITY_END();
}
