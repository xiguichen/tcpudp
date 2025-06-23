#pragma once

#include "message.pb.h"
#include <vector>

class MsgSerializer
{
  public:
    // Serialize Sync message
    std::shared_ptr<std::vector<char>> serializeSync(const Sync &sync);

    // Serialize Ack message
    std::shared_ptr<std::vector<char>> serializeAck(const Ack &ack);

    // Serialize Bind message
    std::shared_ptr<std::vector<char>> serializeBind(const Bind &bind);

    // Serialize Data message
    std::shared_ptr<std::vector<char>> serializeData(const Data &data);
};
