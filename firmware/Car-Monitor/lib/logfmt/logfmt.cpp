#include "logfmt.h"
#include <string.h>

static void put16(uint8_t* p, uint16_t v) { p[0] = v & 0xFF; p[1] = v >> 8; }
static void put32(uint8_t* p, uint32_t v) {
  p[0] = v & 0xFF; p[1] = (v >> 8) & 0xFF; p[2] = (v >> 16) & 0xFF; p[3] = v >> 24;
}
static uint16_t get16(const uint8_t* p) { return (uint16_t)(p[0] | (p[1] << 8)); }
static uint32_t get32(const uint8_t* p) {
  return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

// CRC-16/CCITT-FALSE: poly 0x1021, init 0xFFFF
uint16_t crc16(const uint8_t* data, size_t n) {
  uint16_t crc = 0xFFFF;
  for (size_t i = 0; i < n; i++) {
    crc ^= (uint16_t)data[i] << 8;
    for (int b = 0; b < 8; b++)
      crc = (crc & 0x8000) ? (uint16_t)((crc << 1) ^ 0x1021) : (uint16_t)(crc << 1);
  }
  return crc;
}

size_t writeFileHeader(uint8_t* out, size_t cap) {
  if (cap < LOG_FILE_HEADER_BYTES) return 0;
  out[0] = 'T'; out[1] = 'L'; out[2] = 'O'; out[3] = 'G';
  out[4] = LOG_FORMAT_VER;
  return LOG_FILE_HEADER_BYTES;
}

size_t encodeRecord(uint8_t type, uint32_t seq, uint32_t tMs,
                    const uint8_t* payload, uint8_t len,
                    uint8_t* out, size_t cap) {
  if (len > LOG_MAX_PAYLOAD) return 0;
  size_t total = LOG_HEADER_BYTES + len + LOG_CRC_BYTES;
  if (cap < total) return 0;

  out[0] = LOG_MAGIC;
  out[1] = type;
  out[2] = len;
  put32(out + 3, seq);
  put32(out + 7, tMs);
  if (len) memcpy(out + LOG_HEADER_BYTES, payload, len);
  put16(out + LOG_HEADER_BYTES + len, crc16(out, LOG_HEADER_BYTES + len));
  return total;
}

size_t encodeGps(const GpsRecord& g, uint32_t seq, uint32_t tMs,
                 uint8_t* out, size_t cap) {
  uint8_t p[GPS_PAYLOAD_BYTES];
  put32(p + 0,  (uint32_t)g.latE7);
  put32(p + 4,  (uint32_t)g.lonE7);
  put16(p + 8,  g.speedCentiMph);
  put16(p + 10, (uint16_t)g.altDeciM);
  put16(p + 12, g.headingDeci);
  p[14] = g.sats;
  p[15] = g.status;
  put32(p + 16, g.utcMs);
  return encodeRecord(REC_GPS, seq, tMs, p, GPS_PAYLOAD_BYTES, out, cap);
}

DecodeResult decodeRecord(const uint8_t* buf, size_t n, Record& rec, size_t& consumed) {
  consumed = 0;
  if (n == 0) return DecodeResult::NEED_MORE;
  if (buf[0] != LOG_MAGIC) { consumed = 1; return DecodeResult::BAD_MAGIC; }
  if (n < LOG_HEADER_BYTES) return DecodeResult::NEED_MORE;

  uint8_t len = buf[2];
  if (len > LOG_MAX_PAYLOAD) { consumed = 1; return DecodeResult::BAD_LEN; }

  size_t total = LOG_HEADER_BYTES + len + LOG_CRC_BYTES;
  if (n < total) return DecodeResult::NEED_MORE;

  uint16_t stored = get16(buf + LOG_HEADER_BYTES + len);
  if (crc16(buf, LOG_HEADER_BYTES + len) != stored) {
    consumed = 1;
    return DecodeResult::BAD_CRC;
  }

  rec.type = buf[1];
  rec.len  = len;
  rec.seq  = get32(buf + 3);
  rec.tMs  = get32(buf + 7);
  if (len) memcpy(rec.payload, buf + LOG_HEADER_BYTES, len);
  consumed = total;
  return DecodeResult::OK;
}

bool decodeGps(const Record& rec, GpsRecord& g) {
  if (rec.type != REC_GPS || rec.len != GPS_PAYLOAD_BYTES) return false;
  const uint8_t* p = rec.payload;
  g.latE7         = (int32_t)get32(p + 0);
  g.lonE7         = (int32_t)get32(p + 4);
  g.speedCentiMph = get16(p + 8);
  g.altDeciM      = (int16_t)get16(p + 10);
  g.headingDeci   = get16(p + 12);
  g.sats          = p[14];
  g.status        = p[15];
  g.utcMs         = get32(p + 16);
  return true;
}