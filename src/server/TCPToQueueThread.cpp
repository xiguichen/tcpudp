#include "TcpToQueueThread.h"
#include "TcpDataQueue.h"
#include <iostream>
#include <sys/socket.h>
#include <unistd.h>
#include <vector>

void TcpToQueueThread::run() {
    while (true) {
        char buffer[1024];
        ssize_t bytesRead = readFromSocket(buffer, sizeof(buffer));
        if (bytesRead > 0) {
            enqueueData(buffer, bytesRead);
        }
    }
}

size_t TcpToQueueThread::readFromSocket(char* buffer, size_t bufferSize) {
    size_t bytesRead = recv(socket_, buffer, bufferSize, 0);
    if (bytesRead == 0) {
        std::cout << "Connection closed by peer" << std::endl;
        close(socket_);
    } else if (bytesRead < 0) {
        std::cerr << "Error reading from socket" << std::endl;
    }
    return bytesRead;
}

void TcpToQueueThread::enqueueData(const char* data, size_t length) {
    auto dataVector = std::make_shared<std::vector<char>>(data, data + length);
    TcpDataQueue::getInstance().enqueue(socket_, dataVector);
    std::cout << "Data enqueued" << std::endl;
}
