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

static ElmRecord rpmReading() {            // 41 0C 1A F8  ->  (0x1A*256+0xF8)/4 = 1726 rpm
  ElmRecord e = {};
  e.pid = 0x0C; e.status = ELM_STATUS_OK; e.nbytes = 2; e.raw[0] = 0x1A; e.raw[1] = 0xF8;
  return e;
}

void test_elm_record_size() {
  uint8_t buf[LOG_MAX_RECORD];
  TEST_ASSERT_EQUAL(20, encodeElm(rpmReading(), 1, 1000, buf, sizeof buf));   // 11 + 7 + 2
}

void test_elm_round_trip() {
  uint8_t buf[LOG_MAX_RECORD];
  ElmRecord in = rpmReading();
  size_t n = encodeElm(in, 7, 5000, buf, sizeof buf);
  Record rec; size_t used;
  TEST_ASSERT_EQUAL((int)DecodeResult::OK, (int)decodeRecord(buf, n, rec, used));
  TEST_ASSERT_EQUAL(REC_ELM, rec.type);
  TEST_ASSERT_EQUAL_UINT32(7, rec.seq);
  TEST_ASSERT_EQUAL_UINT32(5000, rec.tMs);
  ElmRecord out;
  TEST_ASSERT_TRUE(decodeElm(rec, out));
  TEST_ASSERT_EQUAL_UINT8(0x0C, out.pid);
  TEST_ASSERT_EQUAL_UINT8(ELM_STATUS_OK, out.status);
  TEST_ASSERT_EQUAL_UINT8(2, out.nbytes);
  TEST_ASSERT_EQUAL_UINT8(0x1A, out.raw[0]);
  TEST_ASSERT_EQUAL_UINT8(0xF8, out.raw[1]);
}

void test_elm_unused_raw_bytes_are_zero() {
  uint8_t buf[LOG_MAX_RECORD];
  ElmRecord in = rpmReading();
  in.raw[2] = 0xEE; in.raw[3] = 0xEE;          // junk past nbytes must not reach the file
  encodeElm(in, 1, 0, buf, sizeof buf);
  TEST_ASSERT_EQUAL_UINT8(0, buf[LOG_HEADER_BYTES + 5]);
  TEST_ASSERT_EQUAL_UINT8(0, buf[LOG_HEADER_BYTES + 6]);
}

void test_elm_no_data_has_no_value() {
  uint8_t buf[LOG_MAX_RECORD];
  ElmRecord nd = {}; nd.pid = 0x0C; nd.status = ELM_STATUS_NO_DATA;
  size_t n = encodeElm(nd, 1, 0, buf, sizeof buf);
  TEST_ASSERT_TRUE(n > 0);
  Record rec; size_t used; ElmRecord out;
  decodeRecord(buf, n, rec, used);
  TEST_ASSERT_TRUE(decodeElm(rec, out));
  TEST_ASSERT_EQUAL_UINT8(ELM_STATUS_NO_DATA, out.status);
  TEST_ASSERT_EQUAL_UINT8(0, out.nbytes);
}

void test_elm_encode_rejects_inconsistent() {
  uint8_t buf[LOG_MAX_RECORD];
  ElmRecord e = rpmReading();
  e.status = ELM_STATUS_NO_DATA;               // NO DATA but claims 2 data bytes
  TEST_ASSERT_EQUAL(0, encodeElm(e, 1, 0, buf, sizeof buf));
  e = rpmReading(); e.nbytes = 5;              // more than 4 bytes
  TEST_ASSERT_EQUAL(0, encodeElm(e, 1, 0, buf, sizeof buf));
  e = rpmReading(); e.status = 3;              // unknown status
  TEST_ASSERT_EQUAL(0, encodeElm(e, 1, 0, buf, sizeof buf));
  TEST_ASSERT_EQUAL(0, encodeElm(rpmReading(), 1, 0, buf, 10));   // no room
}

void test_elm_decode_rejects_bad_payload() {
  Record rec = {}; rec.type = REC_ELM; rec.len = ELM_PAYLOAD_BYTES;
  rec.payload[0] = 0x0C; rec.payload[1] = ELM_STATUS_OK; rec.payload[2] = 2;
  ElmRecord out;
  TEST_ASSERT_TRUE(decodeElm(rec, out));
  rec.payload[2] = 5;                          // nbytes too big
  TEST_ASSERT_FALSE(decodeElm(rec, out));
  rec.payload[2] = 2; rec.payload[1] = ELM_STATUS_NO_DATA;   // NO DATA with data bytes
  TEST_ASSERT_FALSE(decodeElm(rec, out));
  rec.payload[1] = 9;                          // unknown status
  TEST_ASSERT_FALSE(decodeElm(rec, out));
  rec.payload[1] = ELM_STATUS_OK; rec.len = 6; // wrong length
  TEST_ASSERT_FALSE(decodeElm(rec, out));
  rec.len = ELM_PAYLOAD_BYTES; rec.type = REC_GPS;   // wrong type
  TEST_ASSERT_FALSE(decodeElm(rec, out));
}

void test_elm_failed_decode_leaves_output_untouched() {
  Record rec = {}; rec.type = REC_ELM; rec.len = ELM_PAYLOAD_BYTES; rec.payload[2] = 9;
  ElmRecord out = rpmReading();
  TEST_ASSERT_FALSE(decodeElm(rec, out));
  TEST_ASSERT_EQUAL_UINT8(0x0C, out.pid);
  TEST_ASSERT_EQUAL_UINT8(2, out.nbytes);
}

void test_elm_voltage_pseudo_pid() {
  uint8_t buf[LOG_MAX_RECORD];
  ElmRecord v = {}; v.pid = ELM_PID_ADAPTER_VOLTAGE; v.status = ELM_STATUS_OK; v.nbytes = 2;
  v.raw[0] = 12100 >> 8; v.raw[1] = 12100 & 0xFF;          // 12.1 V as millivolts, big-endian
  size_t n = encodeElm(v, 3, 0, buf, sizeof buf);
  Record rec; size_t used; ElmRecord out;
  decodeRecord(buf, n, rec, used);
  TEST_ASSERT_TRUE(decodeElm(rec, out));
  TEST_ASSERT_EQUAL_UINT16(12100, (uint16_t)((out.raw[0] << 8) | out.raw[1]));
}

void test_mixed_gps_and_elm_stream() {
  uint8_t stream[256]; size_t pos = 0;
  pos += encodeGps(sample(), 1, 1000, stream + pos, sizeof stream - pos);
  pos += encodeElm(rpmReading(), 2, 1010, stream + pos, sizeof stream - pos);
  pos += encodeGps(sample(), 3, 1200, stream + pos, sizeof stream - pos);
  size_t at = 0, gps = 0, elm = 0;
  while (at < pos) {
    Record rec; size_t used;
    TEST_ASSERT_EQUAL((int)DecodeResult::OK, (int)decodeRecord(stream + at, pos - at, rec, used));
    GpsRecord g; ElmRecord e;
    if (decodeGps(rec, g)) gps++;
    else if (decodeElm(rec, e)) elm++;
    at += used;
  }
  TEST_ASSERT_EQUAL(2, gps);
  TEST_ASSERT_EQUAL(1, elm);
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
  RUN_TEST(test_elm_record_size);
  RUN_TEST(test_elm_round_trip);
  RUN_TEST(test_elm_unused_raw_bytes_are_zero);
  RUN_TEST(test_elm_no_data_has_no_value);
  RUN_TEST(test_elm_encode_rejects_inconsistent);
  RUN_TEST(test_elm_decode_rejects_bad_payload);
  RUN_TEST(test_elm_failed_decode_leaves_output_untouched);
  RUN_TEST(test_elm_voltage_pseudo_pid);
  RUN_TEST(test_mixed_gps_and_elm_stream);
  return UNITY_END();
}