#pragma once

#include "Socket.h"
#include <atomic>
#include <chrono>
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

    // Async-signal-friendly: only stores the stop flag (no logging/locking/joins
    // and no fd access). The main thread then calls Stop() normally.
    void requestStop();

    // <Summary>
    // Check if the client is running
    // </Summary>
    bool PrepareVC();

    // <Summary>
    // Prepare UDP socket for communication
    // </Summary>
    bool PrepareUdpSocket();

  private:
    std::atomic<bool> running{false};
    std::vector<SocketFd> tcpSockets;
    std::vector<uint32_t> tcpConnectionIds; // maps slotIndex → unique connectionId
    std::atomic<uint32_t> nextConnectionId{1}; // incrementing counter for unique IDs
    std::atomic<SocketFd> udpSocket{static_cast<SocketFd>(-1)};
    struct sockaddr_in udpAddr{};
    struct sockaddr_in remoteUdpAddr{};
    // Serialises UDP recv/send/close. Without it, Stop() closing the socket
    // concurrently with the receive loop could recv()/send() on a recycled FD
    // (on macOS close() does not wake a blocked recv() either). The socket is
    // non-blocking so a send cannot hold this lock across Stop()'s close.
    std::mutex udpMutex;
    std::mutex remoteUdpMutex;

    VirtualChannelSp vc = nullptr;
    std::mutex vcMutex;

    // Serializes the Stop() cleanup with the start of a new Start(). The setup
    // itself is not held under this lock: Stop() must be able to set running=false
    // and cancel an in-progress 32-socket connect.
    std::mutex lifecycleMutex;

    // Start() and a detached ReconnectVC() may both try to rebuild the channel.
    // Recursive because PrepareVC() holds it and calls TeardownVc(), which must
    // take it too when called directly from Start()'s failure paths. This is the
    // single lock that keeps clear() from running between socket pushes.
    std::recursive_mutex prepareMutex;

    std::thread watchdogThread;
    std::atomic<bool> watchdogRunning{false};
    std::mutex watchdogMutex;                 // serializes join() between ReconnectVC and Stop()
    std::atomic<int> reconnectEpoch{0};       // incremented by ReconnectVC; per-slot reconnects
                                              // abort if the epoch changed mid-operation

    // Per-slot reconnect backoff. During a flaky/half-open period a reconnected slot
    // can drop again immediately, so the watchdog would otherwise hammer the same slot
    // every 500ms until the network settles. slotBackoffMs[slot] grows exponentially on
    // each reconnect attempt and slotLastReconnect[slot] records the attempt time; the
    // watchdog skips a dead slot until enough time has passed. Backoff resets to the
    // base once the slot is observed alive again. Only touched by the watchdog thread.
    std::vector<std::chrono::steady_clock::time_point> slotLastReconnect;
    std::vector<int> slotBackoffMs;

    bool ReconnectVC(int maxRetries = 5, int initialBackoffMs = 1000);
    void StartWatchdog();
    // Stop and join the watchdog thread if running. Idempotent.
    void StopWatchdog();
    // Tear down the current virtual channel: stop the watchdog, close+release the VC
    // (which closes its TCP sockets), and clear the TCP socket bookkeeping. Does NOT
    // touch the UDP socket. Safe to call when nothing is active.
    void TeardownVc();
    bool ReconnectSingleSlot(int slotIndex);
};
