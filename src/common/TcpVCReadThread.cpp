#include <cstddef>
#include <chrono>
#include <format>
#include <cstring>
#include "Log.h"
#include "TcpVCReadThread.h"
#include "VcProtocol.h"
#include "PerformanceCounter.h"
#include "Socket.h"

void TcpVCReadThread::run()
{
    log_info("TcpVCReadThread started");

    try
    {
        char buffer[1500];
        auto lastBacklogWarn = std::chrono::steady_clock::time_point{};
        static constexpr auto BACKLOG_WARN_INTERVAL = std::chrono::seconds(1);
        static constexpr size_t BACKLOG_BUFVEC_WARN_BYTES = 64 * 1024;
        static constexpr auto PROCESS_SLOW_WARN_MS = std::chrono::milliseconds(100);

        while (this->isRunning() && this->connection->isConnected())
        {
            size_t dataSize = this->connection->receive(buffer, sizeof(buffer));
            if (dataSize > 0)
            {
                bufferVector.insert(bufferVector.end(), buffer, buffer + dataSize);
            }
            else
            {
                log_info("Connection closed, stopping read thread");
                if(this->disconnectCallback) {
                    this->disconnectCallback(this->connection);
                }
                break;
            }

            auto processingStart = std::chrono::steady_clock::now();
            while (this->hasEnoughData(bufferVector.data(), bufferVector.size()))
            {
                int processedBytes = this->processBuffer(bufferVector);
                if (processedBytes > 0 && processedBytes == bufferVector.size())
                {
                    bufferVector.resize(0);
                }
                else if (processedBytes > 0)
                {
                    bufferVector.erase(bufferVector.begin(), bufferVector.begin() + processedBytes);
                }
                else
                {
                    log_error("Error processing buffer, stopping read thread");
                    break;
                }
            }

            auto processingDur = std::chrono::steady_clock::now() - processingStart;
            auto now = std::chrono::steady_clock::now();
            bool shouldWarn = false;
            if (bufferVector.size() >= BACKLOG_BUFVEC_WARN_BYTES)
            {
                shouldWarn = true;
            }
            if (processingDur >= PROCESS_SLOW_WARN_MS)
            {
                shouldWarn = true;
            }
            if (shouldWarn && (lastBacklogWarn.time_since_epoch().count() == 0 ||
                               (now - lastBacklogWarn) >= BACKLOG_WARN_INTERVAL))
            {
                lastBacklogWarn = now;
                int pendingBytes = SocketBytesAvailable(connection->getSocketFd());
                auto procMs = std::chrono::duration_cast<std::chrono::milliseconds>(processingDur).count();

                uint64_t headMsgId = 0;
                uint16_t headLen = 0;
                if (bufferVector.size() >= sizeof(VCDataPacket))
                {
                    const VCDataPacket *pkt = reinterpret_cast<const VCDataPacket *>(bufferVector.data());
                    headMsgId = pkt->header.messageId;
                    headLen = pkt->dataLength;
                }

                log_warnning(std::format("[VC] ReadThread backlog: conn={} bufVec={}B pendingBytes={} procMs={} headMsgId={} headLen={}B",
                                         connectionIndex,
                                         bufferVector.size(),
                                         pendingBytes,
                                         procMs,
                                         headMsgId,
                                         headLen));
            }
        }

        this->setRunning(false);

        log_info("TcpVCReadThread stopped");
    }
    catch (const std::exception &e)
    {
        log_error(std::format("TcpVCReadThread caught exception: {}", e.what()));
        this->setRunning(false);
        if (this->disconnectCallback)
        {
            this->disconnectCallback(this->connection);
        }
    }
    catch (...)
    {
        log_error("TcpVCReadThread caught unknown exception");
        this->setRunning(false);
        if (this->disconnectCallback)
        {
            this->disconnectCallback(this->connection);
        }
    }
}

int TcpVCReadThread::processBuffer(std::vector<char> &buffer)
{
    auto packetType = static_cast<VcPacketType>(buffer[0]);

    switch (packetType)
    {
    case VcPacketType::DATA: {
        return processDataBuffer(buffer);
        break;
    }
    default: {
        log_error("Unknown packet type received");
        throw std::logic_error("Unknown packet type");
    }
    }
}

inline bool TcpVCReadThread::hasEnoughData(const char *buffer, size_t size)
{
    if (size < VC_MIN_DATA_PACKET_SIZE)
    {
        return false;
    }

    uint8_t packetType = static_cast<uint8_t>(buffer[0]);
    if (packetType == static_cast<uint8_t>(VcPacketType::DATA))
    {
        return hasEnoughDataForData(buffer, size);
    }
    else
    {
        throw std::logic_error("Unknown packet type");
    }
}

bool TcpVCReadThread::hasEnoughDataForData(const char *buffer, size_t size)
{
    if (size < VC_MIN_DATA_PACKET_SIZE)
    {
        return false;
    }

    // Extract the data length from the buffer
    VCDataPacket *dataPacket = (VCDataPacket *)buffer;
    uint16_t dataLength = dataPacket->dataLength;
    if (dataLength > VC_MAX_DATA_PAYLOAD_SIZE)
    {
        throw std::logic_error("Data length exceeds maximum payload size");
    }

    return size >= VC_MIN_DATA_PACKET_SIZE + dataLength;
}

int TcpVCReadThread::processDataBuffer(std::vector<char> &buffer)
{
    VCDataPacket *dataPacket = reinterpret_cast<VCDataPacket *>(buffer.data());
    uint16_t dataLength = dataPacket->dataLength;
    if (dataLength > VC_MAX_DATA_PAYLOAD_SIZE)
    {
        throw std::logic_error("Data length exceeds maximum allowed size");
    }

    auto data = std::make_shared<std::vector<char>>(dataPacket->data, dataPacket->data + dataLength);
    if (dataCallback)
    {
        dataCallback(dataPacket->header.messageId, data);
    }
    return sizeof(VCDataPacket) + dataLength;
}
void TcpVCReadThread::setDataCallback(
    std::function<void(const uint64_t messageId, std::shared_ptr<std::vector<char>> data)> callback)
{
    dataCallback = callback;
}
void TcpVCReadThread::setDisconnectCallback(std::function<void(TcpConnectionSp connection)> callback)
{
    this->disconnectCallback = callback;
}

TcpVCReadThread::~TcpVCReadThread()
{
}
