#include "ubx.h"

size_t ubxEncode(uint8_t cls, uint8_t id, const uint8_t* payload,
                 uint16_t n, uint8_t* out) {
  out[0] = 0xB5;
  out[1] = 0x62;
  out[2] = cls;
  out[3] = id;
  out[4] = n & 0xFF;
  out[5] = n >> 8;
  for (uint16_t i = 0; i < n; i++) out[6 + i] = payload[i];

  // 8-bit Fletcher checksum over class, id, length, and payload
  uint8_t a = 0, b = 0;
  for (size_t i = 2; i < (size_t)6 + n; i++) {
    a += out[i];
    b += a;
  }
  out[6 + n] = a;
  out[7 + n] = b;
  return (size_t)n + 8;
}