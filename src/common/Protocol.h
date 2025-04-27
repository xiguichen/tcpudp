#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <stdexcept>
#include <vector>

// Let's call the protocol as UVT (UDP Over TCP)

struct UvtHeader {
  uint16_t size;
  uint8_t id;
  uint8_t checksum;
};

const size_t HEADER_SIZE = sizeof(UvtHeader);

uint8_t xor_checksum(const uint8_t *buffer, size_t length);

// Utils for UVT protocol
class UvtUtils {
public:
  static uint8_t calculateChecksum(const uint8_t *buffer, size_t length) {
    return xor_checksum(buffer, length);
  }

  static bool validateChecksum(const uint8_t *buffer, size_t length,
                               uint8_t checksum) {
    return xor_checksum(buffer, length) == checksum;
  }

  static void AppendUdpData(const std::vector<uint8_t> &data, uint8_t id,
                            std::vector<uint8_t> &outputBuffer) {
    size_t length = data.size();

    UvtHeader header;
    header.size = static_cast<uint16_t>(length);
    header.id = id;
    header.checksum = calculateChecksum(data.data(), length);

    size_t currentSize = outputBuffer.size();
    outputBuffer.resize(currentSize + HEADER_SIZE + length);
    std::memcpy(outputBuffer.data() + currentSize, &header, HEADER_SIZE);
    std::memcpy(outputBuffer.data() + currentSize + HEADER_SIZE, data.data(), length);
  }

  static void AppendUdpData(const std::vector<char> &data, uint8_t id,
                            std::vector<char> &outputBuffer) {
    size_t length = data.size();

    UvtHeader header;

    header.size = static_cast<uint16_t>(length);
    header.id = id;
    header.checksum = calculateChecksum(reinterpret_cast<const uint8_t *>(data.data()), length);

    size_t currentSize = outputBuffer.size();
    outputBuffer.resize(currentSize + HEADER_SIZE + length);
    std::memcpy(outputBuffer.data() + currentSize, &header, HEADER_SIZE);
    std::memcpy(outputBuffer.data() + currentSize + HEADER_SIZE, data.data(), length);

  }
};
