#include "TcpVCWriteThread.h"
#include "Log.h"
#include "VcProtocol.h"

void TcpVCWriteThread::run()
{
    log_info("TcpVCWriteThread started");
    while(this->isRunning())
    {
        log_info("Waiting for data to send...");
        auto data = writeQueue->dequeue();
        if (!data)
        {
            log_error("Failed to dequeue data for sending");
            break;
        }
        VCHeader* header = reinterpret_cast<VCHeader*>(data->data());
        lastMessageId = header->messageId;
        log_info("Sent message with ID: " + std::to_string(lastMessageId));
        connection->send(data->data(), data->size());
        log_info("type: " + std::to_string(static_cast<uint8_t>(header->type)));
    }
    log_info("TcpVCWriteThread stopped");
}
TcpVCWriteThread::~TcpVCWriteThread()
{
    log_info("~TcpVCWriteThread");
}
