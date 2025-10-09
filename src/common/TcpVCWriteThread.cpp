#include "TcpVCWriteThread.h"
#include "Log.h"
#include "VcProtocol.h"

void TcpVCWriteThread::run()
{
    info("TcpVCWriteThread started");
    while(this->isRunning())
    {
        info("Waiting for data to send...");
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
        info("type: " + std::to_string(static_cast<uint8_t>(header->type)));
    }
    info("TcpVCWriteThread stopped");
}
