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

    auto disconnectCB = [this](TcpConnectionSp connection) {
        log_info("TcpVirtualChannel detected a disconnection");
        if (this->disconnectCallback) {
            this->disconnectCallback(connection);
        }
    };

    for (int i = 0; i < this->connections.size(); ++i)
    {
        auto readThread = TcpVCReadThreadFactory::createThread(this->connections[i]);
        readThread->start();
        readThread->setDataCallback(dataCallback);
        readThread->setDisconnectCallback(disconnectCB);
        readThreads.emplace_back(readThread);
        auto writeThread = TcpVCWriteThreadFactory::createThread(sendQueue, this->connections[i]);
        writeThread->start();
        writeThreads.emplace_back(writeThread);
    }
    opened = true;
}

void TcpVirtualChannel::send(const char *data, size_t size)
{
    if (data != nullptr && size > 0)
    {
        auto messageId = this->lastSendMessageId.fetch_add(1);
        auto messageIdNetwork = messageId;
        log_info(std::format("Sending message with ID: {}", messageId));
        auto dataVec = std::make_shared<std::vector<char>>();

        // Data: | 1 byte type | 8 bytes messageId | 2 bytes data length | N bytes data |
        auto totalPacketSize = sizeof(VCDataPacket) + size;
        VCDataPacket *packet = static_cast<VCDataPacket*>(std::malloc(totalPacketSize));
        packet->header.type = VcPacketType::DATA;
        packet->header.messageId = messageIdNetwork;
        packet->dataLength = static_cast<uint16_t>(size);
        std::memcpy(packet->data, data, size);

        dataVec->resize(totalPacketSize);
        std::memcpy(dataVec->data(), packet, totalPacketSize);
        std::free(packet);

        // Enqueue the data for multiple connections to make sure we can deliver it without problems
        sendQueue->enqueue(dataVec);
    }
}
bool TcpVirtualChannel::isOpen() const
{
    return opened;
}
void TcpVirtualChannel::close()
{
    if (!opened)
    {
        return;
    }

    // Cancle any waiting operations
    this->sendQueue->cancelWait();

    // Close all the connections and stop threads
    log_info("Closing TcpVirtualChannel connections");
    for (auto &conn : connections)
    {
        if (conn && conn->isConnected())
        {
            log_info("Disconnecting a TcpConnection");
            conn->disconnect();
        }
        else 
        {
            log_info("TcpConnection already disconnected");
        }
    }

    log_info("Closing TcpVirtualChannel");
    for (auto &thread : readThreads)
    {
        if (thread)
        {
            log_info("Stopping a read thread");
            thread->stop();
            log_info("Read thread stopped");
        }
    }

    log_info("All read threads stopped");

    log_info("Stopping write threads");
    for (auto &thread : writeThreads)
    {
        if (thread)
        {
            thread->stop();
        }
    }
    log_info("All write threads stopped");
    opened = false;
}
void TcpVirtualChannel::processReceivedData(uint64_t messageId, std::shared_ptr<std::vector<char>> data)
{
    log_info(std::format("Processing received message with ID: {}", messageId));
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

        // Remove the processed message from the map
        receivedDataMap.erase(it);

        // Move to the next expected message ID
        nextMessageId.fetch_add(1);
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

void TcpVirtualChannel::setDisconnectCallback(std::function<void(TcpConnectionSp connection)> callback)
{
    this->disconnectCallback = callback;
}

