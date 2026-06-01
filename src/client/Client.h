#pragma once

#include "Socket.h"
#include <atomic>
#include <mutex>
#include <thread>
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
    std::vector<uint32_t> tcpConnectionIds; // maps slotIndex → unique connectionId
    uint32_t nextConnectionId = 1;          // incrementing counter for unique IDs
    SocketFd udpSocket = -1;
    struct sockaddr_in udpAddr{};
    struct sockaddr_in remoteUdpAddr{};

    VirtualChannelSp vc = nullptr;
    std::mutex vcMutex;

    std::thread watchdogThread;
    std::atomic<bool> watchdogRunning{false};
    std::mutex watchdogMutex;                 // serializes join() between ReconnectVC and Stop()
    std::atomic<int> reconnectEpoch{0};       // incremented by ReconnectVC; per-slot reconnects
                                              // abort if the epoch changed mid-operation

    bool ReconnectVC(int maxRetries = 5, int initialBackoffMs = 1000);
    void StartWatchdog();
    bool ReconnectSingleSlot(int slotIndex);
};
