#include "TcpVirtualChannel.h"
#include "Log.h"
#include "TcpVCReadThreadFactory.h"
#include "TcpVCWriteThreadFactory.h"
#include "VcProtocol.h"
#include <cstring>
#include <format>

void TcpVirtualChannel::open()
{

    auto dataCallback = [this](const uint64_t messageId, std::shared_ptr<std::vector<char>> data) {
        this->processReceivedData(messageId, data);
    };

    auto ackCallback = [this](const uint64_t messageId) {
        this->processAck(messageId);
    };


    for (int i = 0; i < this->connections.size(); ++i)
    {
        auto readThread = TcpVCReadThreadFactory::createThread(this->connections[i]);
        readThread->setDataCallback(dataCallback);
        readThread->setAckCallback(ackCallback);
        readThreads.emplace_back(readThread);
        writeThreads.emplace_back(TcpVCWriteThreadFactory::createThread(sendQueue, this->connections[i]));
    }
    opened = true;
}

void TcpVirtualChannel::send(const char *data, size_t size)
{
    if (data != nullptr && size > 0)
    {
        auto messageId = this->lastSendMessageId.fetch_add(1);
        auto messageIdNetwork = messageId;
        info(std::format("Sending message with ID: {}", messageId));
        auto dataVec = std::make_shared<std::vector<char>>();

        // Data: | 1 byte type | 8 bytes messageId | 2 bytes data length | N bytes data |
        auto totalPacketSize = sizeof(VCDataPacket) + size;
        VCDataPacket *packet = static_cast<VCDataPacket*>(std::malloc(totalPacketSize));
        packet->header.type = VcPacketType::DATA;
        packet->header.messageId = messageIdNetwork;
        packet->dataLength = htons(static_cast<uint16_t>(size));
        std::memcpy(packet->data, data, size);

        dataVec->resize(totalPacketSize);
        std::memcpy(dataVec->data(), packet, totalPacketSize);
        std::free(packet);

        sendQueue->enqueue(dataVec);
    }
}
bool TcpVirtualChannel::isOpen() const
{
    return opened;
}
void TcpVirtualChannel::close()
{
    info("Closing TcpVirtualChannel");
    for (auto &thread : readThreads)
    {
        if (thread)
        {
            thread->stop();
        }
    }
    info("All read threads stopped");

    for (auto &thread : writeThreads)
    {
        if (thread)
        {
            thread->stop();
        }
    }
    info("All write threads stopped");
    opened = false;
}
void TcpVirtualChannel::processReceivedData(uint64_t messageId, std::shared_ptr<std::vector<char>> data)
{
    debug(std::format("Processing received message with ID: {}", messageId));
    std::lock_guard<std::mutex> lock(receivedDataMutex);

    // Check if this is a duplicate message
    if (messageId < nextMessageId)
    {
        return; // Duplicate message, ignore
    }

    // Add the received data to the map
    receivedDataMap[messageId] = data;

    // Process messages in order
    std::map<uint64_t, std::shared_ptr<std::vector<char>>>::iterator it;
    while ((it = receivedDataMap.find(nextMessageId)) != receivedDataMap.end())
    {
        // Call the receive callback with the data
        if (receiveCallback)
        {
            receiveCallback(it->second->data(), it->second->size());
        }

        // Send an acknowledgment for the processed message
        sendAck(it->first);

        // Remove the processed message from the map
        receivedDataMap.erase(it);

        // Move to the next expected message ID
        nextMessageId.fetch_add(1);
    }
}

void TcpVirtualChannel::sendAck(uint64_t messageId)
{
    info(std::format("Sending ACK for message ID: {}", messageId));
    auto ackVec = std::make_shared<std::vector<char>>();

    // Ack:  | 1 byte type | 8 bytes messageId |

    auto messageIdNetwork = messageId;
    VCAckPacket *packet = static_cast<VCAckPacket*>(std::malloc(sizeof(VCAckPacket)));
    packet->header.type = VcPacketType::ACK;
    packet->header.messageId = messageIdNetwork;
    ackVec->resize(sizeof(VCAckPacket));
    std::memcpy(ackVec->data(), packet, sizeof(VCAckPacket));
    std::free(packet);

    sendQueue->enqueue(ackVec);
}

void TcpVirtualChannel::processAck(uint64_t messageId)
{
    info(std::format("Processing ACK for message ID: {}", messageId));
    for (auto &writeThread : writeThreads)
    {
        if (writeThread)
        {
            writeThread->AckCallback(messageId);
        }
    }
}

TcpVirtualChannel::TcpVirtualChannel(std::vector<SocketFd> fds)
{
    for (auto fd : fds)
    {
        connections.emplace_back(std::make_shared<TcpConnection>(fd));
    }
    sendQueue = std::make_shared<BlockingQueue>();
}

