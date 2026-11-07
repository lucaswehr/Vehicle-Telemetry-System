#include <Arduino.h>
#include <unity.h>
#include <math.h>
#include "nmea.h"

const char* RMC = "$GPRMC,221852.00,A,4644.00037,N,11710.18443,W,0.120,,311026,,,D*67";
const char* GGA = "$GPGGA,221852.00,4644.00037,N,11710.18443,W,2,08,1.33,769.3,M,-18.0,M,,0000*6F";
const char* NOFIX = "$GPRMC,221642.00,V,,,,,,,311026,,,N*7B";

void test_checksum_good()  { TEST_ASSERT_TRUE(nmeaChecksumOk(RMC)); }
void test_checksum_bad()   { TEST_ASSERT_FALSE(nmeaChecksumOk("$GPRMC,221852.00,A,4644.00037,N,11710.18443,W,0.120,,311026,,,D*00")); }

void test_rmc_position() {
  GpsFix f;
  TEST_ASSERT_TRUE(nmeaParseLine(RMC, f));
  TEST_ASSERT_TRUE(f.valid);
  TEST_ASSERT_FLOAT_WITHIN(0.00001f, 46.733339f, static_cast<float>(f.lat));
  TEST_ASSERT_FLOAT_WITHIN(0.00001f, -117.169741f, static_cast<float>(f.lon));
  TEST_ASSERT_FLOAT_WITHIN(0.01, 0.0, f.speedMph);
}

void test_gga_sats_and_altitude() {
  GpsFix f;
  TEST_ASSERT_TRUE(nmeaParseLine(GGA, f));
  TEST_ASSERT_EQUAL(8, f.sats);
  TEST_ASSERT_FLOAT_WITHIN(0.1, 769.3, f.altM);
}

void test_no_fix_is_invalid() {
  GpsFix f;
  TEST_ASSERT_TRUE(nmeaParseLine(NOFIX, f));
  TEST_ASSERT_FALSE(f.valid);
}

void test_rmc_date() {
  GpsFix f;
  TEST_ASSERT_TRUE(nmeaParseLine(RMC, f));
  TEST_ASSERT_EQUAL_UINT32(311026, f.utcDate);
}

void test_date_present_without_fix() {
  GpsFix f;
  TEST_ASSERT_TRUE(nmeaParseLine(NOFIX, f));
  TEST_ASSERT_FALSE(f.valid);
  TEST_ASSERT_EQUAL_UINT32(311026, f.utcDate);
}

void setup() {
  delay(2000);
  UNITY_BEGIN();
  RUN_TEST(test_checksum_good);
  RUN_TEST(test_checksum_bad);
  RUN_TEST(test_rmc_position);
  RUN_TEST(test_gga_sats_and_altitude);
  RUN_TEST(test_no_fix_is_invalid);
  RUN_TEST(test_rmc_date);
  RUN_TEST(test_date_present_without_fix);
  UNITY_END();
}

void loop() {
  delay(1000);
}