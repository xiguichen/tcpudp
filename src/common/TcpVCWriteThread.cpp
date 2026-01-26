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
        log_debug("Waiting for data to send...");
        auto data = writeQueue->dequeue();
        // Check for nullptr first - queue may have been cancelled
        if (!data)
        {
            log_info("Thread exiting on null data (queue cancelled or stopped)...");
            break;
        }
        // Also check if we should stop running
        if (!this->isRunning())
        {
            log_info("Thread exiting on stop signal...");
            break;
        }
        VCHeader *header = reinterpret_cast<VCHeader *>(data->data());
        lastMessageId = header->messageId;
        log_debug("Sent message with ID: " + std::to_string(lastMessageId));
        // Only send if the underlying connection is still active
        if (this->connection && this->connection->isConnected()) {
            connection->send(data->data(), data->size());
        } else {
            log_debug("Dropping write: connection not active");
        }
        log_debug("type: " + std::to_string(static_cast<uint8_t>(header->type)));
    }

    log_info("TcpVCWriteThread stopped");
}
TcpVCWriteThread::~TcpVCWriteThread()
{
    log_info("~TcpVCWriteThread");
}
