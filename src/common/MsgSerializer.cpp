#include "MsgSerializer.h"
#include "MsgType.h"

std::shared_ptr<std::vector<char>> MsgSerializer::serializeSync(const Sync &sync)
{
    std::shared_ptr<std::vector<char>> buffer = std::make_shared<std::vector<char>>();
    buffer->data()[0] = static_cast<char>(MsgType::MSG_SYNC);
    sync.SerializeToArray(buffer->data() + 1, buffer->size() - 1);
    return buffer;
}

std::shared_ptr<std::vector<char>> MsgSerializer::serializeAck(const Ack &ack)
{
    std::shared_ptr<std::vector<char>> buffer = std::make_shared<std::vector<char>>();
    buffer->data()[0] = static_cast<char>(MsgType::MSG_ACK);
    ack.SerializeToArray(buffer->data() + 1, buffer->size() - 1);
    return buffer;
}

std::shared_ptr<std::vector<char>> MsgSerializer::serializeBind(const Bind &bind)
{
    std::shared_ptr<std::vector<char>> buffer = std::make_shared<std::vector<char>>();
    buffer->data()[0] = static_cast<char>(MsgType::MSG_BIND);
    bind.SerializeToArray(buffer->data() + 1, buffer->size() - 1);
    return buffer;
}

std::shared_ptr<std::vector<char>> MsgSerializer::serializeData(const Data &data)
{
    std::shared_ptr<std::vector<char>> buffer = std::make_shared<std::vector<char>>();
    buffer->data()[0] = static_cast<char>(MsgType::MSG_DATA);
    data.SerializeToArray(buffer->data() + 1, buffer->size() - 1);
    return buffer;
}

