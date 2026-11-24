#include <unity.h>
#include <string.h>
#include "places.h"

void setUp() {}
void tearDown() {}

static const PlaceEntry T[] = {
  {4673, -11718, "Pullman", "WA"},
  {4673, -11700, "Moscow", "ID"},
  {4688, -11736, "Colfax", "WA"},
  {4766, -11743, "Spokane", "WA"},
};
static const size_t N = sizeof T / sizeof T[0];

static bool find(float lat, float lon, PlaceMatch& m, float maxKm = PLACE_MAX_KM) {
  return placeNearest(T, N, lat, lon, maxKm, m);
}

void test_exact_position_matches() {
  PlaceMatch m;
  TEST_ASSERT_TRUE(find(46.73f, -117.18f, m));
  TEST_ASSERT_EQUAL_STRING("Pullman", m.name);
  TEST_ASSERT_EQUAL_STRING("WA", m.state);
  TEST_ASSERT_TRUE(m.km < 1.0f);
}

void test_picks_the_nearest_not_the_first() {
  PlaceMatch m;
  TEST_ASSERT_TRUE(find(46.74f, -117.01f, m));            // right next to Moscow, ID
  TEST_ASSERT_EQUAL_STRING("Moscow", m.name);
  TEST_ASSERT_EQUAL_STRING("ID", m.state);
}

void test_between_two_towns_picks_the_closer() {
  PlaceMatch m;
  TEST_ASSERT_TRUE(find(46.73f, -117.12f, m));            // 0.06 deg from Pullman, 0.12 deg from Moscow
  TEST_ASSERT_EQUAL_STRING("Pullman", m.name);
}

void test_too_far_gives_no_match_and_leaves_output() {
  PlaceMatch m = {"keep", "kp", -1.0f};
  TEST_ASSERT_FALSE(find(40.0f, -100.0f, m));             // the middle of Nebraska
  TEST_ASSERT_EQUAL_STRING("keep", m.name);
  TEST_ASSERT_FLOAT_WITHIN(0.001f, -1.0f, m.km);
}

void test_distance_cutoff_is_respected() {
  PlaceMatch m;
  // about 0.17 degrees of longitude (13 km at this latitude) from Pullman and 0.17 from Moscow's side
  TEST_ASSERT_TRUE(find(46.73f, -117.35f, m, 30.0f));
  TEST_ASSERT_FALSE(find(46.30f, -117.18f, m, 30.0f));    // 47 km south of Pullman
  TEST_ASSERT_TRUE(find(46.30f, -117.18f, m, 60.0f));     // same point with a wider limit
}

void test_longitude_scales_with_latitude() {
  // 0.2 degrees of longitude is ~15.2 km at 46.7 N (not 22 km): inside a 16 km cutoff of Pullman
  PlaceMatch m;
  TEST_ASSERT_TRUE(placeNearest(T, 1, 46.73f, -117.38f, 16.0f, m));
  TEST_ASSERT_FALSE(placeNearest(T, 1, 46.73f, -117.38f, 14.0f, m));
}

void test_distance_value_is_about_right() {
  PlaceMatch m;
  TEST_ASSERT_TRUE(placeNearest(T, 1, 46.83f, -117.18f, 50.0f, m));   // 0.1 deg north of Pullman
  TEST_ASSERT_FLOAT_WITHIN(0.3f, 11.1f, m.km);
}

void test_empty_and_null_tables() {
  PlaceMatch m;
  TEST_ASSERT_FALSE(placeNearest(T, 0, 46.7f, -117.2f, 50, m));
  TEST_ASSERT_FALSE(placeNearest(nullptr, 3, 46.7f, -117.2f, 50, m));
}

void test_invalid_coordinates_rejected() {
  PlaceMatch m;
  TEST_ASSERT_FALSE(find(91.0f, -117.0f, m));
  TEST_ASSERT_FALSE(find(46.7f, 200.0f, m));
  TEST_ASSERT_FALSE(find(NAN, -117.0f, m));
}

void test_date_line_wraps() {
  static const PlaceEntry fiji[] = {{-1714, 17974, "Savusavu", "" }};      // 179.74 E
  PlaceMatch m;
  TEST_ASSERT_TRUE(placeNearest(fiji, 1, -17.14f, -179.9f, 50.0f, m));     // just across the 180 line
  TEST_ASSERT_FLOAT_WITHIN(2.0f, 38.2f, m.km);                              // 0.36 deg of longitude at 17 S
}

void test_southern_hemisphere() {
  static const PlaceEntry syd[] = {{-3387, 15121, "Sydney", "NSW"}};
  PlaceMatch m;
  TEST_ASSERT_TRUE(placeNearest(syd, 1, -33.8688f, 151.2093f, 10.0f, m));
  TEST_ASSERT_EQUAL_STRING("Sydney", m.name);
}

// ---- the generated table that ships in the firmware ----
void test_generated_table_has_places() {
  TEST_ASSERT_TRUE(PLACES_COUNT > 0);
  for (size_t i = 0; i < PLACES_COUNT; i++) {
    TEST_ASSERT_TRUE(PLACES[i].name != nullptr && PLACES[i].name[0] != 0);
    TEST_ASSERT_TRUE(PLACES[i].state != nullptr);
    TEST_ASSERT_TRUE(strlen(PLACES[i].name) <= 23);
  }
}

void test_generated_table_finds_pullman() {
  PlaceMatch m;
  TEST_ASSERT_TRUE(placeNearest(PLACES, PLACES_COUNT, 46.7313f, -117.1796f, PLACE_MAX_KM, m));
  TEST_ASSERT_EQUAL_STRING("Pullman", m.name);
  TEST_ASSERT_EQUAL_STRING("WA", m.state);
}

void test_generated_table_open_ocean_has_no_place() {
  PlaceMatch m;
  TEST_ASSERT_FALSE(placeNearest(PLACES, PLACES_COUNT, 40.0f, -140.0f, PLACE_MAX_KM, m));
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_exact_position_matches);
  RUN_TEST(test_picks_the_nearest_not_the_first);
  RUN_TEST(test_between_two_towns_picks_the_closer);
  RUN_TEST(test_too_far_gives_no_match_and_leaves_output);
  RUN_TEST(test_distance_cutoff_is_respected);
  RUN_TEST(test_longitude_scales_with_latitude);
  RUN_TEST(test_distance_value_is_about_right);
  RUN_TEST(test_empty_and_null_tables);
  RUN_TEST(test_invalid_coordinates_rejected);
  RUN_TEST(test_date_line_wraps);
  RUN_TEST(test_southern_hemisphere);
  RUN_TEST(test_generated_table_has_places);
  RUN_TEST(test_generated_table_finds_pullman);
  RUN_TEST(test_generated_table_open_ocean_has_no_place);
  return UNITY_END();
}
