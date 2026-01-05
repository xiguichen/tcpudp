#include "TcpVCWriteThread.h"
#include "Log.h"
#include "VcProtocol.h"
#include "PerformanceCounter.h"

void TcpVCWriteThread::run()
{
    log_info("TcpVCWriteThread started");

    this->connection->setOnSendTimeoutCallback([this](char *buffer, size_t size) {
        log_error("Send timeout occurred for message ID: " + std::to_string(lastMessageId));
        // Re-enqueue the data for retransmission
        auto dataCopy = std::make_shared<std::vector<char>>(buffer, buffer + size);
        writeQueue->enqueue(dataCopy);
    });

    while (this->isRunning())
    {
        log_info("Waiting for data to send...");
        auto data = writeQueue->dequeue();
        if (!data)
        {
            log_error("Failed to dequeue data for sending");
            break;
        }
        VCHeader *header = reinterpret_cast<VCHeader *>(data->data());
        lastMessageId = header->messageId;
        log_debug("Sent message with ID: " + std::to_string(lastMessageId));
        connection->send(data->data(), data->size());
        log_debug("type: " + std::to_string(static_cast<uint8_t>(header->type)));
    }

    log_info("TcpVCWriteThread stopped");
}
TcpVCWriteThread::~TcpVCWriteThread()
{
    log_info("~TcpVCWriteThread");
}
