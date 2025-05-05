#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>
#include "Log.h"
#include <format>
#include <vector>
#include "Socket.h"
#include <format>

using namespace Logger;

// Let's call the protocol as UVT (UDP Over TCP)

struct UvtHeader {
  uint16_t size;
  uint8_t id;
  uint8_t checksum;
};

struct MsgBind {
  uint32_t clientId;
};

struct MsgBindResponse {
  uint8_t status;
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

  template <typename T>
  static void AppendUdpData(const std::vector<T> &data, uint8_t id,
                            std::vector<T> &outputBuffer) {
    size_t length = data.size();

    UvtHeader header;
    header.size = htons(static_cast<uint16_t>(length));
    header.id = id;
    header.checksum = calculateChecksum(reinterpret_cast<const uint8_t *>(data.data()), length);

    size_t currentSize = outputBuffer.size();
    outputBuffer.resize(currentSize + HEADER_SIZE + length);
    std::memcpy(outputBuffer.data() + currentSize, &header, HEADER_SIZE);
    std::memcpy(outputBuffer.data() + currentSize + HEADER_SIZE, data.data(), length);
  }


  //  Extract UDP data from the TCP data buffer
  //  Returns the data that not yet extracted from the data buffer
  template <typename T>
  static std::vector<T> ExtractUdpData(const std::vector<T> &data, std::vector<T> &outputBuffer) {
      outputBuffer.clear();

      Log::getInstance().info("Begin ExtractUdpData");

      if (data.size() < HEADER_SIZE) {
          Log::getInstance().info("not enough data for decode header");
          return data; // Not enough data for a header
      }

      const UvtHeader* header = reinterpret_cast<const UvtHeader*>(data.data());
      uint16_t messageLength = ntohs(header->size);
      Log::getInstance().info(std::format("Message Id: {}, Message Length: {}", header->id, messageLength));

      if (HEADER_SIZE + messageLength > data.size()) {
          Log::getInstance().info(std::format("not enough data for decode data. Expected: {}, Actual: {}", HEADER_SIZE + messageLength, data.size()));
          return data; // Not enough data for a full message
      }

      const uint8_t* messageData = reinterpret_cast<const uint8_t*>(data.data() + HEADER_SIZE);
      uint8_t checksum = UvtUtils::calculateChecksum(messageData, messageLength);

      if (checksum == header->checksum) {
          outputBuffer.insert(outputBuffer.end(), messageData, messageData + messageLength);
      } else {
          throw std::logic_error("ExtractUdpData: Checksum verify failed");
          return data;
      }

      Log::getInstance().info("End ExtractUdpData");
      return std::vector<T>(data.begin() + HEADER_SIZE + messageLength, data.end());
  }

};
