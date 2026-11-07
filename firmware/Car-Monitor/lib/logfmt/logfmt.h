#pragma once
#include <stdint.h>
#include <stddef.h>

constexpr uint8_t LOG_MAGIC             = 0xA5;
constexpr uint8_t LOG_FORMAT_VER        = 1;
constexpr size_t  LOG_FILE_HEADER_BYTES = 5;
constexpr size_t  LOG_HEADER_BYTES      = 11;   // magic, type, len, seq(4), time(4)
constexpr size_t  LOG_CRC_BYTES         = 2;
constexpr size_t  LOG_MAX_PAYLOAD       = 64;
constexpr size_t  LOG_MAX_RECORD        = LOG_HEADER_BYTES + LOG_MAX_PAYLOAD + LOG_CRC_BYTES;
constexpr size_t  GPS_PAYLOAD_BYTES     = 20;

enum RecordType : uint8_t { REC_GPS = 1, REC_IMU = 2, REC_ELM = 3 };

struct GpsRecord {
  int32_t  latE7;           // degrees * 1e7
  int32_t  lonE7;
  uint16_t speedCentiMph;   // mph * 100
  int16_t  altDeciM;        // meters * 10
  uint16_t headingDeci;     // degrees * 10
  uint8_t  sats;
  uint8_t  status;          // 0 LINK_LOST, 1 NO_FIX, 2 OK
  uint32_t utcMs;           // GPS time of day in ms
};

struct Record {
  uint8_t  type;
  uint32_t seq;
  uint32_t tMs;
  uint8_t  len;
  uint8_t  payload[LOG_MAX_PAYLOAD];
};

enum class DecodeResult { OK, NEED_MORE, BAD_MAGIC, BAD_LEN, BAD_CRC };

uint16_t crc16(const uint8_t* data, size_t n);

size_t writeFileHeader(uint8_t* out, size_t cap);            // returns 5, or 0 if no room

// Generic encoder. Returns bytes written, or 0 on error (no room, payload too big).
size_t encodeRecord(uint8_t type, uint32_t seq, uint32_t tMs,
                    const uint8_t* payload, uint8_t len,
                    uint8_t* out, size_t cap);

size_t encodeGps(const GpsRecord& g, uint32_t seq, uint32_t tMs,
                 uint8_t* out, size_t cap);

// Decode one record from the start of buf.
//   OK:        consumed = record size
//   NEED_MORE: consumed = 0 (wait for more bytes)
//   anything else: consumed = 1 (caller skips one byte and tries again)
DecodeResult decodeRecord(const uint8_t* buf, size_t n, Record& rec, size_t& consumed);

bool decodeGps(const Record& rec, GpsRecord& g);