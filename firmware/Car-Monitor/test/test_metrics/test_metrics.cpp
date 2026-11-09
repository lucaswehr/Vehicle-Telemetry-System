#include <unity.h>
#include "metrics.h"

void test_empty_stat_reads_zero() {
  Stat s;
  TEST_ASSERT_EQUAL_UINT32(0, s.count);
  TEST_ASSERT_EQUAL_UINT32(0, statAvg(s));
  TEST_ASSERT_EQUAL_UINT32(0, statMin(s));
  TEST_ASSERT_EQUAL_UINT32(0, s.maxV);
}
void test_single_sample() {
  Stat s;
  statRecord(s, 250);
  TEST_ASSERT_EQUAL_UINT32(1, s.count);
  TEST_ASSERT_EQUAL_UINT32(250, statAvg(s));
  TEST_ASSERT_EQUAL_UINT32(250, statMin(s));
  TEST_ASSERT_EQUAL_UINT32(250, s.maxV);
}
void test_min_max_avg() {
  Stat s;
  statRecord(s, 100);
  statRecord(s, 300);
  statRecord(s, 200);
  TEST_ASSERT_EQUAL_UINT32(3, s.count);
  TEST_ASSERT_EQUAL_UINT32(200, statAvg(s));
  TEST_ASSERT_EQUAL_UINT32(100, statMin(s));
  TEST_ASSERT_EQUAL_UINT32(300, s.maxV);
}
void test_zero_is_a_valid_sample() {
  Stat s;
  statRecord(s, 0);
  statRecord(s, 10);
  TEST_ASSERT_EQUAL_UINT32(0, statMin(s));
  TEST_ASSERT_EQUAL_UINT32(5, statAvg(s));
}
void test_reset_clears_everything() {
  Stat s;
  statRecord(s, 999);
  statReset(s);
  TEST_ASSERT_EQUAL_UINT32(0, s.count);
  TEST_ASSERT_EQUAL_UINT32(0, s.maxV);
  statRecord(s, 5);
  TEST_ASSERT_EQUAL_UINT32(5, statMin(s));   // old min must not survive a reset
}
void test_large_sum_does_not_overflow() {
  Stat s;
  for (int i = 0; i < 10; i++) statRecord(s, 1000000000u);   // 10e9 exceeds 32 bits
  TEST_ASSERT_EQUAL_UINT32(1000000000u, statAvg(s));
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_empty_stat_reads_zero);
  RUN_TEST(test_single_sample);
  RUN_TEST(test_min_max_avg);
  RUN_TEST(test_zero_is_a_valid_sample);
  RUN_TEST(test_reset_clears_everything);
  RUN_TEST(test_large_sum_does_not_overflow);
  return UNITY_END();
}
