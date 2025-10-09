#include "TcpQueueToUdpThread.h"
#include <Protocol.h>
#include <Socket.h>
#include <Log.h>
#include "SocketManager.h"
#include "Configuration.h"
#include <format>

using namespace Logger;

void TcpQueueToUdpThread::run() {
    info("Starting TcpQueueToUdpThread");

    info(std::format("UDP socket: {}, queue: {:p}", udpSocket_, static_cast<void*>(&queue_)));

    while (SocketManager::isServerRunning()) {
        processData();
    }
}

void TcpQueueToUdpThread::processData() {
    // Check running state before processing
    if (!SocketManager::isServerRunning()) return;

    info(std::format("Processing data from TCP queue to UDP, queue: {:p}", static_cast<void*>(&queue_)));
    auto data = queue_.dequeue();
    sendDataViaUdp(data);
}

void TcpQueueToUdpThread::sendDataViaUdp(std::shared_ptr<std::vector<char>> data) {

    sockaddr_in udpAddr;
    udpAddr.sin_family = AF_INET;
    udpAddr.sin_port = htons(Configuration::getInstance()->getPortNumber());
    inet_pton(AF_INET, Configuration::getInstance()->getSocketAddress().c_str(), &udpAddr.sin_addr);

    info("Server -> Server (UDP Data)");

    ssize_t sentBytes = SendUdpData(udpSocket_, data->data(), data->size(), 0, (struct sockaddr*)&udpAddr, sizeof(udpAddr));
    if (sentBytes < 0) {
        error(std::format("Failed to send data via UDP, socket: {}", udpSocket_));
    }
}
