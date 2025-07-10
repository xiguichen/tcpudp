#include "StateBindSent.h"
#include "MsgType.h"
#include "MsgDeserializer.h"
#include "MsgSerializer.h"
#include <Log.h>

using namespace Logger;

void StateBindSent::processData(const std::vector<char> &data)
{
    if(data.empty())
    {
        this->_nextState = CommunicateStates::STATE_ERROR;
        Logger::Log::getInstance().error("Received empty data in StateBindSent");
    }

    // The first byte of data indicates the message type
    MsgType msgType = MsgDeserializer::deserializeMsgType(data);
    if(msgType != MsgType::MSG_DATA)
    {
        Logger::Log::getInstance().error("Unexpected message type in StateBindSent: " + std::to_string(static_cast<int>(msgType)));
        this->_nextState = CommunicateStates::STATE_ERROR;
        return;
    }

    // Deserialize the Data message

    auto messageData = MsgDeserializer::deserializeData(data.data() + sizeof(uint8_t), data.size() - sizeof(uint8_t));
    if(!messageData)
    {
        Logger::Log::getInstance().error("Failed to deserialize Data message in StateBindSent");
        this->_nextState = CommunicateStates::STATE_ERROR;
        return;
    }

}
void StateBindSent::transitionToNextState()
{
}


