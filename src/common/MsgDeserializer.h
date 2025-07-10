#pragma once

#include "MsgType.h"
#include <vector>
#include <message.pb.h>

class MsgDeserializer
{
public:

    // Deserialize message type from raw data
  static MsgType deserializeMsgType(const std::vector<char> &data);

  // Deserialize Sync from raw data
  static std::shared_ptr<Sync> deserializeSync(const void* data, int size);

  // Deserialize Ack from raw data
  static std::shared_ptr<Ack> deserializeAck(const void* data, int size);

  // Deserialize Bind from raw data
  static std::shared_ptr<Bind> deserializeBind(const void* data, int size);

  // Deserialize Data from raw Data
  static std::shared_ptr<Data> deserializeData(const void* data, int size);

};

