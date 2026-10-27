#pragma once
#include <Log.h>
#include <Socket.h>
#include <atomic>
#include <chrono>
#include <mutex>
#include <unordered_map>

class Server
{
  public:
    Server() = default;
    ~Server() = default;

    void start()
    {
        running = true;
        log_info("Starting server...");
        SocketInit();

        // Server logic to start listening for connections would go here
        if (Listen())
        {
            // Accept connections
            AcceptConnections();
        }
    }

    void stop();

    // Async-signal-friendly stop request: one atomic store, no logging, locks,
    // allocation, thread joins or fd access. Calling stop() directly from a
    // signal handler can deadlock (the interrupted thread may hold the log/mutex
    // lock) and is not async-signal-safe. The accept loop polls with a 250ms
    // timeout, so it observes the flag without a cross-thread shutdown() on an
    // fd number that stop() may have already closed and the OS reused.
    void requestStop() { running.store(false, std::memory_order_release); }

    bool isRunning() const { return running.load(std::memory_order_acquire); }

  private:
    std::atomic<bool> running{false};

    bool Listen();
    void AcceptConnections();

    std::atomic<SocketFd> serverSocket{static_cast<SocketFd>(-1)};

    // Protects socketToConnId/clientConnSlots: the accept thread and the VC
    // disconnect callbacks (which run on VC IO threads) both touch them.
    std::mutex mapsMutex;

    // Maps socket FD → connectionId for sockets in the accumulation phase
    // (before the VC is created and slots are assigned).
    std::unordered_map<SocketFd, uint32_t> socketToConnId;

    // After VC creation: maps clientId → (connectionId → slotIndex).
    // Used to force-close a specific connection by ID during reconnect,
    // bypassing the deadSlots check which may be stale due to half-open TCP.
    std::unordered_map<uint32_t, std::unordered_map<uint32_t, int>> clientConnSlots;
};
