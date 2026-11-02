#include <unity.h>
#include "ubx.h"

void test_cfg_rate_5hz() {
  const uint8_t payload[] = {0xC8, 0x00, 0x01, 0x00, 0x01, 0x00};
  const uint8_t expected[] = {0xB5, 0x62, 0x06, 0x08, 0x06, 0x00,
                              0xC8, 0x00, 0x01, 0x00, 0x01, 0x00,
                              0xDE, 0x6A};
  uint8_t out[32];
  size_t len = ubxEncode(0x06, 0x08, payload, sizeof payload, out);
  TEST_ASSERT_EQUAL(sizeof expected, len);
  TEST_ASSERT_EQUAL_UINT8_ARRAY(expected, out, sizeof expected);
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_cfg_rate_5hz);
  return UNITY_END();
}