#include "StateSyncSent.h"
#include <Log.h>
#include <MsgType.h>
#include <MsgDeserializer.h>
#include <MsgSerializer.h>
#include "StateBindSent.h"
#include "StateError.h"

using namespace Logger;

void StateSyncSent::processData(const std::vector<char> &data)
{
    if (data.empty())
    {
        // Handle empty data case, possibly transition to error state
        Log::getInstance().error("Received empty data in StateSyncSent");
        _nextState = CommunicateStates::STATE_ERROR;
        return;
    }

    // The first byte of data indicates the message type
    // The only expected message type in this state is MSG_ACK
    MsgType msgType = MsgDeserializer::deserializeMsgType(data);
    if (msgType != MsgType::MSG_ACK)
    {
        Log::getInstance().error("Unexpected message type in StateSyncSent: " + std::to_string(static_cast<int>(msgType)));
        _nextState = CommunicateStates::STATE_ERROR;
        return;
    }

    // Deserialize the ACK message
    auto ack = MsgDeserializer::deserializeAck(data.data(), data.size());

    if(ack->acknumber() != _context->syncCounter.get())
    {
        Log::getInstance().error("ACK number does not match expected sync counter in StateSyncSent");
        _nextState = CommunicateStates::STATE_ERROR;
        return;
    }

    // Send the Bind message
    Bind bindMsg;
    bindMsg.set_sequencenumber(_context->syncCounter.incrementAndGet());
    auto msgBinary = MsgSerializer::serializeBind(bindMsg);
    _context->tcpSendQueue.enqueue(msgBinary);

    Log::getInstance().info("Processed ACK in StateSyncSent, transitioning to STATE_BIND_SENT");

    this->_nextState = CommunicateStates::STATE_BIND_SENT;
}

void StateSyncSent::transitionToNextState()
{
    if (_context == nullptr)
    {
        Log::getInstance().error("Context is not set in StateSyncSent");
        return;
    }

    // Transition to the next state based on the nextState variable
    switch (_nextState)
    {
        case CommunicateStates::STATE_BIND_SENT:
            _context->setState(std::make_shared<StateBindSent>(_context));
            break;
        case CommunicateStates::STATE_ERROR:
            _context->setState(std::make_shared<StateError>(_context));
            break;
        default:
            Log::getInstance().error("Unhandled next state in StateSyncSent: " + std::to_string(static_cast<int>(_nextState)));
            break;
    }
}

