#pragma once
#include <stdint.h>
#include <stddef.h>

// Builds a UBX message into out[]. Returns total length (payload length + 8).
// out must hold at least payloadLen + 8 bytes.
size_t ubxEncode(uint8_t cls, uint8_t id, const uint8_t* payload,
                 uint16_t payloadLen, uint8_t* out);