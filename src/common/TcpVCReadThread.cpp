#include <cstddef>
#include <format>
#include <cstring>
#include "Log.h"
#include "TcpVCReadThread.h"
#include "VcProtocol.h"
#include "PerformanceCounter.h"

void TcpVCReadThread::run()
{
    log_info("TcpVCReadThread started");

    try
    {
        char buffer[1500];
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

    std::shared_ptr<std::vector<char>> data = std::make_shared<std::vector<char>>();
    data->reserve(dataLength);
    data->insert(data->end(), dataPacket->data, dataPacket->data + dataLength);
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
