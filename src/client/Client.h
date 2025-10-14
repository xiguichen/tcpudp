#pragma once

#include "Socket.h"
#include <atomic>
#include "VirtualChannel.h"

class Client
{
  public:
    Client() = default;
    ~Client() = default;

    // <Summary>
    // Start the client operations
    // </Summary>
    void Start();

    // <Summary>
    // Stop the client operations
    // </Summary>
    void Stop();

    // <Summary>
    // Check if the client is running
    // </Summary>
    bool PrepareVC();

    // <Summary>
    // Prepare UDP socket for communication
    // </Summary>
    bool PrepareUdpSocket();

  private:
    bool running = false;
    std::vector<SocketFd> tcpSockets;
    SocketFd udpSocket = -1;
    struct sockaddr_in udpAddr{};
    struct sockaddr_in remoteUdpAddr{};

    VirtualChannelSp vc = nullptr;
};
