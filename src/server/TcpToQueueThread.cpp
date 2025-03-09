#include "TcpToQueueThread.h"
#include "TcpDataQueue.h"
#include <iostream>
#include <sys/socket.h>
#include <unistd.h>
#include <vector>
#include <Socket.h>
#include <arpa/inet.h>
#include <Log.h>
#include <format>

using namespace Logger;

void TcpToQueueThread::run() {
    while (true) {
        char buffer[65535];
        ssize_t bytesRead = readFromSocket(buffer, sizeof(buffer));
        if (bytesRead > 0) {
            enqueueData(buffer, bytesRead);
        }
        else
        {
            break;
        }
    }
}

size_t TcpToQueueThread::readFromSocket(char* buffer, size_t bufferSize) {
    // First, read the length of the message
    uint32_t messageLength = 0;
    size_t lengthBytesRead = RecvTcpData(socket_, &messageLength, sizeof(messageLength), 0);
    if (lengthBytesRead != sizeof(messageLength)) {
        if (lengthBytesRead == 0) {
            Log::getInstance().error("Connection closed by peer. Length=0");
            close(socket_);
        } else {
            Log::getInstance().error("Connection closed by peer. Length not correct");
        }
        return 0;
    }

    // Convert message length from network byte order to host byte order
    messageLength = ntohl(messageLength);
    
    Log::getInstance().info(std::format("TCP -> Queue: Message size: {}", messageLength));

    // Ensure the buffer is large enough
    if (messageLength > bufferSize) {
        std::cerr << "Buffer size is too small for the incoming message" << std::endl;
        Log::getInstance().error(std::format("TCP -> Queue: Buffer size less than {}", messageLength));
        exit(1);
        return 0;
    }

    // Now, read the actual message based on the length
    size_t bytesRead = RecvTcpData(socket_, buffer, messageLength, 0);
    if (bytesRead == 0) {

        Log::getInstance().error("TCP -> Queue: Connection closed by peer.");
        close(socket_);
    } else if (bytesRead < 0) {
        Log::getInstance().error("TCP -> Queue: Connection closed by peer.");
    }
    return bytesRead;
}

void TcpToQueueThread::enqueueData(const char* data, size_t length) {
    auto dataVector = std::make_shared<std::vector<char>>(data, data + length);
    TcpDataQueue::getInstance().enqueue(socket_, dataVector);
    Log::getInstance().info("TCP -> Queue: Data enqueued.");
}
