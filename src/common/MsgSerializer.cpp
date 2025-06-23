#include "MsgSerializer.h"


std::shared_ptr<std::vector<char>> MsgSerializer::serializeSync(const Sync &sync)
{
    std::shared_ptr<std::vector<char>> buffer = std::make_shared<std::vector<char>>();
    sync.SerializeToArray(buffer->data(), buffer->size());
    return buffer;
}

std::shared_ptr<std::vector<char>> MsgSerializer::serializeAck(const Ack &ack)
{
    std::shared_ptr<std::vector<char>> buffer = std::make_shared<std::vector<char>>();
    ack.SerializeToArray(buffer->data(), buffer->size());
    return buffer;
}

std::shared_ptr<std::vector<char>> MsgSerializer::serializeBind(const Bind &bind)
{
    std::shared_ptr<std::vector<char>> buffer = std::make_shared<std::vector<char>>();
    bind.SerializeToArray(buffer->data(), buffer->size());
    return buffer;
}

std::shared_ptr<std::vector<char>> MsgSerializer::serializeData(const Data &data)
{
    std::shared_ptr<std::vector<char>> buffer = std::make_shared<std::vector<char>>();
    data.SerializeToArray(buffer->data(), buffer->size());
    return buffer;
}

