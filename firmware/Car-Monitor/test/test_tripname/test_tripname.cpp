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

// ---- utcToLocalDate (Pacific Standard Time, offset -8) ----
static uint32_t local(uint32_t d, uint32_t t, int off = -8) {
  uint32_t out = 0;
  return utcToLocalDate(d, t, off, out) ? out : 0xFFFFFFFFu;
}
void test_local_evening_is_previous_day() {
  TEST_ASSERT_EQUAL_UINT32(81126, local(91126, 42200));      // 09 Nov 04:22 UTC -> 08 Nov
}
void test_local_daytime_same_day() {
  TEST_ASSERT_EQUAL_UINT32(91126, local(91126, 160000));     // 16:00 UTC -> 08:00 local
}
void test_local_midnight_boundary() {
  TEST_ASSERT_EQUAL_UINT32(91126, local(91126, 80000));      // 08:00:00 UTC is 00:00 local, same day
  TEST_ASSERT_EQUAL_UINT32(81126, local(91126, 75959));      // 07:59:59 UTC is still the day before
}
void test_local_month_rollback() {
  TEST_ASSERT_EQUAL_UINT32(301126, local(11226, 30000));     // 01 Dec 03:00 UTC -> 30 Nov
}
void test_local_year_rollback() {
  TEST_ASSERT_EQUAL_UINT32(311226, local(10127, 30000));     // 01 Jan 2027 03:00 UTC -> 31 Dec 2026
}
void test_local_leap_day() {
  TEST_ASSERT_EQUAL_UINT32(290228, local(10328, 30000));     // 2028 is a leap year
  TEST_ASSERT_EQUAL_UINT32(280227, local(10327, 30000));     // 2027 is not
}
void test_local_positive_offset_rolls_forward() {
  TEST_ASSERT_EQUAL_UINT32(10127, local(311226, 220000, 5)); // 31 Dec 22:00 UTC, +5 -> 01 Jan
}
void test_local_rejects_bad_input() {
  TEST_ASSERT_EQUAL_UINT32(0xFFFFFFFFu, local(0, 120000));          // no date yet
  TEST_ASSERT_EQUAL_UINT32(0xFFFFFFFFu, local(310226, 120000));     // 31 Feb
  TEST_ASSERT_EQUAL_UINT32(0xFFFFFFFFu, local(91126, 246000));      // time out of range
  TEST_ASSERT_EQUAL_UINT32(0xFFFFFFFFu, local(91126, 126000));      // 60 minutes
  TEST_ASSERT_EQUAL_UINT32(0xFFFFFFFFu, local(91126, 120000, 30));  // offset out of range
}
void test_local_date_feeds_trip_name() {
  char b[40];
  uint32_t d = local(91126, 42200);                                  // the Nov 8 evening case
  TEST_ASSERT_TRUE(tripName(b, sizeof b, d, 1));
  TEST_ASSERT_EQUAL_STRING("/2026-11-08_trip1.bin", b);
}

// ---- usDstActive ----
void test_dst_summer_and_winter() {
  TEST_ASSERT_TRUE(usDstActive(150726, 120000, -8));         // 15 Jul 2026
  TEST_ASSERT_FALSE(usDstActive(150126, 120000, -8));        // 15 Jan
  TEST_ASSERT_FALSE(usDstActive(151226, 120000, -8));        // 15 Dec
  TEST_ASSERT_TRUE(usDstActive(150426, 0, -8));              // 15 Apr
  TEST_ASSERT_TRUE(usDstActive(151026, 235959, -8));         // 15 Oct
}
void test_dst_start_2026_pacific() {                         // second Sunday in March 2026 is the 8th
  TEST_ASSERT_FALSE(usDstActive(70326, 120000, -8));         // Saturday the 7th
  TEST_ASSERT_FALSE(usDstActive(80326, 95959, -8));          // 09:59:59 UTC, just before 2:00 PST
  TEST_ASSERT_TRUE(usDstActive(80326, 100000, -8));          // 10:00:00 UTC = 2:00 PST -> daylight
  TEST_ASSERT_TRUE(usDstActive(90326, 120000, -8));
}
void test_dst_end_2026_pacific() {                           // first Sunday in November 2026 is the 1st
  TEST_ASSERT_TRUE(usDstActive(311026, 120000, -8));         // 31 Oct
  TEST_ASSERT_TRUE(usDstActive(11126, 85959, -8));           // 08:59:59 UTC, just before 2:00 PDT
  TEST_ASSERT_FALSE(usDstActive(11126, 90000, -8));          // 09:00:00 UTC = 2:00 PDT -> standard
  TEST_ASSERT_FALSE(usDstActive(21126, 120000, -8));
}
void test_dst_other_years() {
  TEST_ASSERT_FALSE(usDstActive(140327, 95959, -8));         // 2027: second Sunday in March is the 14th
  TEST_ASSERT_TRUE(usDstActive(140327, 100000, -8));
  TEST_ASSERT_TRUE(usDstActive(71127, 85959, -8));           // 2027: first Sunday in November is the 7th
  TEST_ASSERT_FALSE(usDstActive(71127, 90000, -8));
  TEST_ASSERT_FALSE(usDstActive(120328, 95959, -8));         // 2028 (leap year): March 12
  TEST_ASSERT_TRUE(usDstActive(120328, 100000, -8));
  TEST_ASSERT_TRUE(usDstActive(51128, 85959, -8));           // 2028: November 5
  TEST_ASSERT_FALSE(usDstActive(51128, 90000, -8));
}
void test_dst_other_us_zone() {                              // Eastern: switches at 07:00 and 06:00 UTC
  TEST_ASSERT_FALSE(usDstActive(80326, 65959, -5));
  TEST_ASSERT_TRUE(usDstActive(80326, 70000, -5));
  TEST_ASSERT_TRUE(usDstActive(11126, 55959, -5));
  TEST_ASSERT_FALSE(usDstActive(11126, 60000, -5));
}
void test_dst_rejects_bad_input() {
  TEST_ASSERT_FALSE(usDstActive(0, 120000, -8));             // no date yet
  TEST_ASSERT_FALSE(usDstActive(150726, 246000, -8));        // time out of range
  TEST_ASSERT_FALSE(usDstActive(150726, 120000, 1));         // not a US zone offset
}
void test_local_date_uses_dst() {
  uint32_t out = 0;
  int off = -8 + (usDstActive(150726, 73000, -8) ? 1 : 0);   // 15 Jul 07:30 UTC, daylight time
  TEST_ASSERT_TRUE(utcToLocalDate(150726, 73000, off, out));
  TEST_ASSERT_EQUAL_UINT32(150726, out);                     // 00:30 PDT on the 15th (a fixed -8 would say the 14th)
  off = -8 + (usDstActive(150726, 65959, -8) ? 1 : 0);       // 06:59:59 UTC
  TEST_ASSERT_TRUE(utcToLocalDate(150726, 65959, off, out));
  TEST_ASSERT_EQUAL_UINT32(140726, out);                     // 23:59:59 PDT on the 14th
}

// ---- parseTripName ----
void test_parse_dated_names() {
  uint32_t k = 0;
  TEST_ASSERT_TRUE(parseTripName("/2026-11-09_trip16.bin", k));
  TEST_ASSERT_EQUAL_UINT32(261109016u, k);
  TEST_ASSERT_TRUE(parseTripName("2026-11-09_trip1.bin", k));      // no leading slash (newer SD library)
  TEST_ASSERT_EQUAL_UINT32(261109001u, k);
  TEST_ASSERT_TRUE(parseTripName("/2026-11-09_TRIP7.BIN", k));     // FAT may report upper case
}
void test_parse_old_style_names() {
  uint32_t k = 0;
  TEST_ASSERT_TRUE(parseTripName("/trip_0001.bin", k));
  TEST_ASSERT_EQUAL_UINT32(1, k);
  TEST_ASSERT_TRUE(parseTripName("trip_0042.bin", k));
  TEST_ASSERT_EQUAL_UINT32(42, k);
}
void test_parse_rejects_other_files() {
  uint32_t k = 0;
  TEST_ASSERT_FALSE(parseTripName("/notes.txt", k));
  TEST_ASSERT_FALSE(parseTripName("/System Volume Information", k));
  TEST_ASSERT_FALSE(parseTripName("/2026-11-09_trip.bin", k));     // no trip number
  TEST_ASSERT_FALSE(parseTripName("/2026-11-09_trip0.bin", k));
  TEST_ASSERT_FALSE(parseTripName("/2026-13-09_trip1.bin", k));    // month 13
  TEST_ASSERT_FALSE(parseTripName("/2026-11-09_trip1.bak", k));
  TEST_ASSERT_FALSE(parseTripName("/2026-11-09_trip1234.bin", k));
  TEST_ASSERT_FALSE(parseTripName("/my_2026-11-09_trip1.bin", k));
  TEST_ASSERT_FALSE(parseTripName("/trip_00.bin", k));
  TEST_ASSERT_FALSE(parseTripName("/2124-11-09_trip1.bin", k));    // year out of range
  TEST_ASSERT_FALSE(parseTripName("", k));
  TEST_ASSERT_FALSE(parseTripName(nullptr, k));
}
void test_parse_sort_order_is_chronological() {
  uint32_t old1 = 0, a = 0, b = 0, c = 0, d = 0;
  parseTripName("/trip_0099.bin", old1);
  parseTripName("/2026-11-09_trip2.bin", a);
  parseTripName("/2026-11-09_trip10.bin", b);                      // 10 must sort after 2 (not as text)
  parseTripName("/2026-11-10_trip1.bin", c);
  parseTripName("/2027-01-02_trip1.bin", d);
  TEST_ASSERT_TRUE(old1 < a);
  TEST_ASSERT_TRUE(a < b);
  TEST_ASSERT_TRUE(b < c);
  TEST_ASSERT_TRUE(c < d);
}
void test_parse_accepts_what_tripname_makes() {
  char name[40];
  uint32_t k = 0;
  TEST_ASSERT_TRUE(tripName(name, sizeof name, 91126, 123));
  TEST_ASSERT_TRUE(parseTripName(name, k));
  TEST_ASSERT_EQUAL_UINT32(261109123u, k);
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_name_format);
  RUN_TEST(test_name_end_of_year);
  RUN_TEST(test_rejects_missing_date);
  RUN_TEST(test_rejects_nonsense_date);
  RUN_TEST(test_rejects_small_buffer);
  RUN_TEST(test_rejects_bad_trip_number);
  RUN_TEST(test_local_evening_is_previous_day);
  RUN_TEST(test_local_daytime_same_day);
  RUN_TEST(test_local_midnight_boundary);
  RUN_TEST(test_local_month_rollback);
  RUN_TEST(test_local_year_rollback);
  RUN_TEST(test_local_leap_day);
  RUN_TEST(test_local_positive_offset_rolls_forward);
  RUN_TEST(test_local_rejects_bad_input);
  RUN_TEST(test_local_date_feeds_trip_name);
  RUN_TEST(test_dst_summer_and_winter);
  RUN_TEST(test_dst_start_2026_pacific);
  RUN_TEST(test_dst_end_2026_pacific);
  RUN_TEST(test_dst_other_years);
  RUN_TEST(test_dst_other_us_zone);
  RUN_TEST(test_dst_rejects_bad_input);
  RUN_TEST(test_local_date_uses_dst);
  RUN_TEST(test_parse_dated_names);
  RUN_TEST(test_parse_old_style_names);
  RUN_TEST(test_parse_rejects_other_files);
  RUN_TEST(test_parse_sort_order_is_chronological);
  RUN_TEST(test_parse_accepts_what_tripname_makes);
  return UNITY_END();
}
