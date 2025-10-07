#include "TcpVCWriteThread.h"
#include "Log.h"
#include "VcProtocol.h"

void TcpVCWriteThread::run()
{
    ackQueue = std::make_shared<BlockingQueue>();
    while(this->isRunning())
    {
        auto data = writeQueue->dequeue();
        if (!data)
        {
            error("Failed to dequeue data for sending");
            continue;
        }

        VCHeader* header = reinterpret_cast<VCHeader*>(data->data());
        lastMessageId = header->messageId;
        info("Sent message with ID: " + std::to_string(lastMessageId));
        connection->send(data->data(), data->size());
        if(header->type == VcPacketType::DATA)
        {
            info("Waiting for ACK for message ID: " + std::to_string(lastMessageId));
            // Wait for the ack
            auto ack = ackQueue->dequeue();
            if (!ack)
            {
                error("Failed to receive ACK for message ID: " + std::to_string(lastMessageId));
                break;
            }
        }
    }
}
void TcpVCWriteThread::AckCallback(uint64_t messageId)
{
    if (messageId == lastMessageId && ackQueue)
    {
        info("Received ACK for message ID: " + std::to_string(messageId));
        ackQueue->enqueue(std::make_shared<std::vector<char>>());
    }
}
