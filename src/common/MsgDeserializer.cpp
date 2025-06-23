#include "MsgDeserializer.h"
MsgType MsgDeserializer::deserializeMsgType(const std::vector<char> &data)
{
    if (data.size() < sizeof(uint8_t))
    {
        throw std::runtime_error("Data too short to contain message type");
    }
    return static_cast<MsgType>(data[0]);
}
std::shared_ptr<Sync> MsgDeserializer::deserializeSync(const std::vector<char> &data)
{
    auto sync = std::make_shared<Sync>();
    sync->ParseFromArray(data.data(), data.size());
    return sync;
}
std::shared_ptr<Ack> MsgDeserializer::deserializeAck(const std::vector<char> &data)
{
    auto ack = std::make_shared<Ack>();
    ack->ParseFromArray(data.data(), data.size());
    return ack;
}
std::shared_ptr<Bind> MsgDeserializer::deserializeBind(const std::vector<char> &data)
{
    auto bind = std::make_shared<Bind>();
    bind->ParseFromArray(data.data(), data.size());
    return bind;
}
std::shared_ptr<Data> MsgDeserializer::deserializeData(const std::vector<char> &data)
{
    auto messageData = std::make_shared<Data>();
    messageData->ParseFromArray(data.data(), data.size());
    return messageData;
}
