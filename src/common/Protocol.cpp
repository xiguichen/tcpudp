#include "Protocol.h"
#include <cstddef>

uint8_t xor_checksum(const uint8_t *buffer, size_t length) {
  uint8_t xor_val = 0;
  for (size_t i = 0; i < length; i++) {
    xor_val ^= buffer[i];
  }
  return xor_val;
}

