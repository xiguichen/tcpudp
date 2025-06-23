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
  static std::shared_ptr<Sync> deserializeSync(const std::vector<char> &data);

  // Deserialize Ack from raw data
  static std::shared_ptr<Ack> deserializeAck(const std::vector<char> &data);

  // Deserialize Bind from raw data
  static std::shared_ptr<Bind> deserializeBind(const std::vector<char> &data);

  // Deserialize Data from raw Data
  static std::shared_ptr<Data> deserializeData(const std::vector<char> &data);

};

