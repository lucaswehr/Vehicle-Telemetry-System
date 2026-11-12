#include <unity.h>
#include "preroll.h"

void setUp() {}
void tearDown() {}

static void pushByte(PreRoll& p, uint8_t v) { uint8_t r[3] = {v, v, v}; preRollPush(p, r, 3); }

void test_empty() {
  PreRoll p;
  size_t n = 99;
  TEST_ASSERT_EQUAL_UINT8(0, preRollCount(p));
  TEST_ASSERT_TRUE(preRollGet(p, 0, n) == nullptr);
  TEST_ASSERT_EQUAL_UINT32(0, n);
}
void test_keeps_order_oldest_first() {
  PreRoll p;
  for (uint8_t i = 1; i <= 5; i++) pushByte(p, i);
  TEST_ASSERT_EQUAL_UINT8(5, preRollCount(p));
  for (uint8_t i = 0; i < 5; i++) {
    size_t n = 0;
    const uint8_t* r = preRollGet(p, i, n);
    TEST_ASSERT_EQUAL_UINT32(3, n);
    TEST_ASSERT_EQUAL_UINT8(i + 1, r[0]);
  }
}
void test_overwrites_oldest_when_full() {
  PreRoll p;
  for (int i = 1; i <= PREROLL_SLOTS + 10; i++) pushByte(p, (uint8_t)i);
  TEST_ASSERT_EQUAL_UINT8(PREROLL_SLOTS, preRollCount(p));
  size_t n = 0;
  TEST_ASSERT_EQUAL_UINT8(11, preRollGet(p, 0, n)[0]);                       // 1..10 were pushed out
  TEST_ASSERT_EQUAL_UINT8(PREROLL_SLOTS + 10, preRollGet(p, PREROLL_SLOTS - 1, n)[0]);
  TEST_ASSERT_TRUE(preRollGet(p, PREROLL_SLOTS, n) == nullptr);
}
void test_variable_lengths_and_bad_sizes() {
  PreRoll p;
  uint8_t big[PREROLL_SLOT_BYTES + 1] = {0};
  TEST_ASSERT_FALSE(preRollPush(p, big, sizeof big));
  TEST_ASSERT_FALSE(preRollPush(p, big, 0));
  TEST_ASSERT_TRUE(preRollPush(p, big, PREROLL_SLOT_BYTES));
  uint8_t small[2] = {7, 8};
  TEST_ASSERT_TRUE(preRollPush(p, small, 2));
  size_t n = 0;
  preRollGet(p, 0, n); TEST_ASSERT_EQUAL_UINT32(PREROLL_SLOT_BYTES, n);
  preRollGet(p, 1, n); TEST_ASSERT_EQUAL_UINT32(2, n);
  TEST_ASSERT_EQUAL_UINT8(2, preRollCount(p));
}
void test_clear_then_reuse() {
  PreRoll p;
  for (int i = 0; i < 40; i++) pushByte(p, (uint8_t)i);
  preRollClear(p);
  TEST_ASSERT_EQUAL_UINT8(0, preRollCount(p));
  pushByte(p, 42);
  size_t n = 0;
  TEST_ASSERT_EQUAL_UINT8(1, preRollCount(p));
  TEST_ASSERT_EQUAL_UINT8(42, preRollGet(p, 0, n)[0]);
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_empty);
  RUN_TEST(test_keeps_order_oldest_first);
  RUN_TEST(test_overwrites_oldest_when_full);
  RUN_TEST(test_variable_lengths_and_bad_sizes);
  RUN_TEST(test_clear_then_reuse);
  return UNITY_END();
}
