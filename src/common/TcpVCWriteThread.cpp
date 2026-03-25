#include "TcpVCWriteThread.h"
#include "Log.h"
#include "VcProtocol.h"
#include "PerformanceCounter.h"

void TcpVCWriteThread::run()
{
    log_info("TcpVCWriteThread started");

    this->connection->setOnSendTimeoutCallback([this](char *buffer, size_t size) {
        auto queueDepth = writeQueue->size();
        log_info("[PERF-DIAG] Send timeout for msgID: " + std::to_string(lastMessageId)
            + ", re-enqueueing. Queue depth: " + std::to_string(queueDepth));
        auto dataCopy = std::make_shared<std::vector<char>>(buffer, buffer + size);
        writeQueue->enqueue(dataCopy);
    });

    while (this->isRunning())
    {
        auto data = writeQueue->dequeue();
        if (!data)
        {
            log_info("Thread exiting on null data (queue cancelled or stopped)");
            break;
        }
        if (!this->isRunning())
        {
            log_info("Thread exiting on stop signal");
            break;
        }
        VCHeader *header = reinterpret_cast<VCHeader *>(data->data());
        lastMessageId = header->messageId;
        if (this->connection && this->connection->isConnected()) {
            connection->send(data->data(), data->size());
        }
    }

    log_info("TcpVCWriteThread stopped");
}
TcpVCWriteThread::~TcpVCWriteThread()
{
    log_info("~TcpVCWriteThread");
}
