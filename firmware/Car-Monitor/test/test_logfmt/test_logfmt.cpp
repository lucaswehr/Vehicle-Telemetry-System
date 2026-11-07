#include <unity.h>
#include <string.h>
#include "logfmt.h"

static GpsRecord sample() {
  GpsRecord g;
  g.latE7 = 467333390;          // 46.733339
  g.lonE7 = -1171697410;        // -117.169741 (negative, so signed handling gets tested)
  g.speedCentiMph = 3452;
  g.altDeciM = 2345;
  g.headingDeci = 845;
  g.sats = 8;
  g.status = 2;
  g.utcMs = 221852200;
  return g;
}

void test_crc_known_vector() {
  const uint8_t d[] = {'1','2','3','4','5','6','7','8','9'};
  TEST_ASSERT_EQUAL_HEX16(0x29B1, crc16(d, sizeof d));   // standard CCITT-FALSE check value
}

void test_gps_record_size() {
  uint8_t buf[LOG_MAX_RECORD];
  TEST_ASSERT_EQUAL(33, encodeGps(sample(), 1, 1000, buf, sizeof buf));
}

void test_round_trip() {
  uint8_t buf[LOG_MAX_RECORD];
  GpsRecord in = sample();
  size_t n = encodeGps(in, 42, 123456, buf, sizeof buf);

  Record rec; size_t used;
  TEST_ASSERT_EQUAL((int)DecodeResult::OK, (int)decodeRecord(buf, n, rec, used));
  TEST_ASSERT_EQUAL(n, used);
  TEST_ASSERT_EQUAL(REC_GPS, rec.type);
  TEST_ASSERT_EQUAL_UINT32(42, rec.seq);
  TEST_ASSERT_EQUAL_UINT32(123456, rec.tMs);

  GpsRecord out;
  TEST_ASSERT_TRUE(decodeGps(rec, out));
  TEST_ASSERT_EQUAL_INT32(in.latE7, out.latE7);
  TEST_ASSERT_EQUAL_INT32(in.lonE7, out.lonE7);
  TEST_ASSERT_EQUAL_UINT16(in.speedCentiMph, out.speedCentiMph);
  TEST_ASSERT_EQUAL_INT16(in.altDeciM, out.altDeciM);
  TEST_ASSERT_EQUAL_UINT16(in.headingDeci, out.headingDeci);
  TEST_ASSERT_EQUAL_UINT8(in.sats, out.sats);
  TEST_ASSERT_EQUAL_UINT8(in.status, out.status);
  TEST_ASSERT_EQUAL_UINT32(in.utcMs, out.utcMs);
}

void test_flipped_bit_rejected() {
  uint8_t buf[LOG_MAX_RECORD];
  size_t n = encodeGps(sample(), 1, 1000, buf, sizeof buf);
  for (size_t i = 1; i < n; i++) {            // flip one bit in every byte after the magic
    uint8_t copy[LOG_MAX_RECORD];
    memcpy(copy, buf, n);
    copy[i] ^= 0x04;
    Record rec; size_t used;
    DecodeResult r = decodeRecord(copy, n, rec, used);
    TEST_ASSERT_TRUE(r != DecodeResult::OK);
  }
}

void test_truncated_needs_more() {
  uint8_t buf[LOG_MAX_RECORD];
  size_t n = encodeGps(sample(), 1, 1000, buf, sizeof buf);
  Record rec; size_t used;
  TEST_ASSERT_EQUAL((int)DecodeResult::NEED_MORE, (int)decodeRecord(buf, n - 1, rec, used));
  TEST_ASSERT_EQUAL(0, used);
}

void test_bad_magic() {
  uint8_t buf[LOG_MAX_RECORD];
  size_t n = encodeGps(sample(), 1, 1000, buf, sizeof buf);
  buf[0] = 0x00;
  Record rec; size_t used;
  TEST_ASSERT_EQUAL((int)DecodeResult::BAD_MAGIC, (int)decodeRecord(buf, n, rec, used));
  TEST_ASSERT_EQUAL(1, used);
}

void test_resync_after_garbage() {
  uint8_t stream[256];
  size_t pos = 0;
  const uint8_t garbage[] = {0x00, 0xA5, 0x01, 0x14, 0x77};   // includes a fake record start
  memcpy(stream + pos, garbage, sizeof garbage); pos += sizeof garbage;
  pos += encodeGps(sample(), 10, 1000, stream + pos, sizeof stream - pos);
  pos += encodeGps(sample(), 11, 1200, stream + pos, sizeof stream - pos);

  size_t at = 0, good = 0, expectSeq = 10;
  while (at < pos) {
    Record rec; size_t used;
    DecodeResult r = decodeRecord(stream + at, pos - at, rec, used);
    if (r == DecodeResult::OK) { TEST_ASSERT_EQUAL_UINT32(expectSeq++, rec.seq); good++; at += used; }
    else if (r == DecodeResult::NEED_MORE) break;
    else at += used;                                           // skip one byte and keep looking
  }
  TEST_ASSERT_EQUAL(2, good);
}

void test_oversize_payload_rejected() {
  uint8_t buf[LOG_MAX_RECORD + 10];
  uint8_t big[LOG_MAX_PAYLOAD + 1] = {0};
  TEST_ASSERT_EQUAL(0, encodeRecord(REC_GPS, 0, 0, big, sizeof big, buf, sizeof buf));
}

void test_no_room_rejected() {
  uint8_t small[10];
  TEST_ASSERT_EQUAL(0, encodeGps(sample(), 1, 1000, small, sizeof small));
}

void test_file_header() {
  uint8_t h[LOG_FILE_HEADER_BYTES];
  TEST_ASSERT_EQUAL(5, writeFileHeader(h, sizeof h));
  TEST_ASSERT_EQUAL_UINT8_ARRAY("TLOG", h, 4);
  TEST_ASSERT_EQUAL_UINT8(LOG_FORMAT_VER, h[4]);
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_crc_known_vector);
  RUN_TEST(test_gps_record_size);
  RUN_TEST(test_round_trip);
  RUN_TEST(test_flipped_bit_rejected);
  RUN_TEST(test_truncated_needs_more);
  RUN_TEST(test_bad_magic);
  RUN_TEST(test_resync_after_garbage);
  RUN_TEST(test_oversize_payload_rejected);
  RUN_TEST(test_no_room_rejected);
  RUN_TEST(test_file_header);
  return UNITY_END();
}