#include <cstddef>
#include <format>
#include <cstring>
#include "Log.h"
#include "TcpVCReadThread.h"
#include "VcProtocol.h"

void TcpVCReadThread::run()
{
    log_info("TcpVCReadThread Start running");

    char buffer[1500];
    while (this->isRunning() && this->connection->isConnected())
    {
        // clear buffer
        memset(buffer, 0, sizeof(buffer));

        size_t dataSize = this->connection->receive(buffer, sizeof(buffer));
        if (dataSize > 0)
        {
            bufferVector.insert(bufferVector.end(), buffer, buffer + dataSize);
        }
        else
        {
            log_info("No data received, stopping read thread");
            if(this->disconnectCallback) {
                this->disconnectCallback(this->connection);
            }
            break; // Connection closed or error
        }

        // Ack:  | 1 byte type | 8 bytes messageId |
        while (this->hasEnoughData(bufferVector.data(), bufferVector.size()))
        {
            log_info(std::format("Buffer size before processing: {}", bufferVector.size()));
            int processedBytes = this->processBuffer(bufferVector);
            if (processedBytes > 0)
            {
                bufferVector.erase(bufferVector.begin(), bufferVector.begin() + processedBytes);
            }
            else
            {
                log_error("Error processing buffer, stopping read thread");
                break; // No more complete messages to process
            }
            log_info(std::format("Buffer size after processing: {}", bufferVector.size()));
        }
    }

    this->setRunning(false);

    log_info("TcpVCReadThread end running");
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

bool TcpVCReadThread::hasEnoughData(const char *buffer, size_t size)
{
    if (size < VC_MIN_DATA_PACKET_SIZE) // Minimum size for type + messageId + data length
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

    std::shared_ptr<std::vector<char>> data = std::make_shared<std::vector<char>>(dataLength);
    std::copy(dataPacket->data, dataPacket->data + dataLength, data->data());
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
    log_debug("~TcpVCReadThread");
}
