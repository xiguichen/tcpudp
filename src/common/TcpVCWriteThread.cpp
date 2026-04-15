#include "TcpVCWriteThread.h"
#include "Log.h"
#include "VcProtocol.h"
#include <chrono>
#include <format>

void TcpVCWriteThread::run()
{
    log_info("TcpVCWriteThread started");

    static constexpr auto SLOW_SEND_WARN_MS = std::chrono::milliseconds(100);

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
        if (this->connection && this->connection->isConnected()) {
            uint64_t messageId = 0;
            uint16_t payloadLen = 0;
            if (data->size() >= sizeof(VCDataPacket))
            {
                const VCDataPacket *packet = reinterpret_cast<const VCDataPacket *>(data->data());
                messageId = packet->header.messageId;
                payloadLen = packet->dataLength;
            }

            auto start = std::chrono::steady_clock::now();
            connection->diagMarkSendStart(messageId);
            connection->send(data->data(), data->size());
            connection->diagMarkSendEnd(messageId);
            auto dur = std::chrono::steady_clock::now() - start;
            if (dur >= SLOW_SEND_WARN_MS)
            {
                auto durMs = std::chrono::duration_cast<std::chrono::milliseconds>(dur).count();
                log_warnning(std::format("[VC] Slow TCP send: conn={} messageId={} payload={}B total={}B sendMs={}",
                                         connectionIndex,
                                         messageId,
                                         payloadLen,
                                         data->size(),
                                         durMs));
            }
        }
    }

    log_info("TcpVCWriteThread stopped");
}
TcpVCWriteThread::~TcpVCWriteThread()
{
    log_info("~TcpVCWriteThread");
}
