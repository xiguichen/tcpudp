#pragma once

#include "Log.h"
#include "Socket.h"
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <format>
#include <set>
#include <vector>

using namespace Logger;

// Let's call the protocol as UVT (UDP Over TCP)

struct UvtHeader {
  uint16_t size;
  uint8_t id;
  uint8_t checksum;
};

typedef struct _MsgBind {
  uint32_t clientId;
} MsgBind, *pMsgBind;

typedef struct _MsgBindResponse {
  uint32_t connectionId;
} MsgBindResponse, *pMsgBindResponse;

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
    header.checksum = calculateChecksum(
        reinterpret_cast<const uint8_t *>(data.data()), length);

    size_t currentSize = outputBuffer.size();
    outputBuffer.resize(currentSize + HEADER_SIZE + length);
    std::memcpy(outputBuffer.data() + currentSize, &header, HEADER_SIZE);
    std::memcpy(outputBuffer.data() + currentSize + HEADER_SIZE, data.data(),
                length);
  }

  //  Extract UDP data from the TCP data buffer
  //  Returns the data that not yet extracted from the data buffer
  template <typename T>
  static std::vector<T> ExtractUdpData(const std::vector<T> &data,
                                       std::vector<T> &outputBuffer) {
    outputBuffer.clear();

    Log::getInstance().info("Begin ExtractUdpData");

    if (data.size() < HEADER_SIZE) {
      Log::getInstance().info("not enough data for decode header");
      return data; // Not enough data for a header
    }

    const UvtHeader *header = reinterpret_cast<const UvtHeader *>(data.data());
    uint16_t messageLength = ntohs(header->size);
    Log::getInstance().info(std::format("Message Id: {}, Message Length: {}",
                                        header->id, messageLength));

    if (HEADER_SIZE + messageLength > data.size()) {
      Log::getInstance().info(std::format(
          "not enough data for decode data. Expected: {}, Actual: {}",
          HEADER_SIZE + messageLength, data.size()));
      return data; // Not enough data for a full message
    }

    const uint8_t *messageData =
        reinterpret_cast<const uint8_t *>(data.data() + HEADER_SIZE);
    uint8_t checksum = UvtUtils::calculateChecksum(messageData, messageLength);

    if (checksum == header->checksum) {
      outputBuffer.insert(outputBuffer.end(), messageData,
                          messageData + messageLength);
    } else {
      throw std::logic_error("ExtractUdpData: Checksum verify failed");
      return data;
    }

    Log::getInstance().info("End ExtractUdpData");
    return std::vector<T>(data.begin() + HEADER_SIZE + messageLength,
                          data.end());
  }

  template <typename T>
  static void AppendMsgBind(const MsgBind &bind, std::vector<T> &outputBuffer) {
    size_t currentSize = outputBuffer.size();
    outputBuffer.resize(currentSize + sizeof(MsgBind));
    std::copy(reinterpret_cast<const T *>(&bind),
              reinterpret_cast<const T *>(&bind) + sizeof(MsgBind),
              outputBuffer.data() + currentSize);
  }

  template <typename T>
  static bool ExtractMsgBind(const std::vector<T> &data, MsgBind &bind) {
    if (data.size() < sizeof(MsgBind)) {
      return false; // Not enough data for a MsgBind
    }
    std::copy(data.begin(), data.begin() + sizeof(MsgBind),
              reinterpret_cast<T *>(&bind));
    return true;
  }

  template <typename T>
  static void AppendMsgBindResponse(const MsgBindResponse &bindResponse,
                                    std::vector<T> &outputBuffer) {
    size_t currentSize = outputBuffer.size();
    std::copy(reinterpret_cast<const T *>(&bindResponse),
              reinterpret_cast<const T *>(&bindResponse) +
                  sizeof(MsgBindResponse),
              outputBuffer.data() + currentSize);
  }

  template <typename T>
  static bool ExtractMsgBindResponse(const std::vector<T> &data,
                                     MsgBindResponse &bindResponse) {
    if (data.size() < sizeof(MsgBindResponse)) {
      return false; // Not enough data for a MsgBindResponse
    }
    std::copy(data.begin(), data.begin() + sizeof(MsgBindResponse),
              reinterpret_cast<T *>(&bindResponse));
    return true;
  }
};

typedef struct Connection {
  uint32_t clientId;
  uint32_t connectionId;
} Connection, *pConnection;

class ConnectionManager {
public:
  // Create a connection
  Connection *createConnection(uint32_t clientId) {
    // Create a new connection and add it to the list
    pConnection connection = new Connection();
    connection->clientId = clientId;
    connection->connectionId = generateConnectionId();
    connections.push_back(connection);
    return connection;
  }

  // Destructor
  ~ConnectionManager() {
    // Clean up all connections
    for (Connection *connection : connections) {
      delete connection;
    }
  }

  // Generate a new connection id
  uint32_t generateConnectionId() {
    uint32_t connectionId = rand() % 1000000; // Random id for example
    while (connectionIds.find(connectionId) != connectionIds.end()) {
      connectionId = rand() % 1000000; // Regenerate if id already exists
    }
    connectionIds.insert(connectionId);
    return connectionId;
  }

private:
  std::vector<pConnection> connections;

  // Set of connection ids
  std::set<uint32_t> connectionIds;
};
