#pragma once
#include <Log.h>
#include <Socket.h>
#include <chrono>
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

    void stop()
    {
        log_info("Stopping server...");
        running = false;
        if (serverSocket != -1)
        {
            SocketClose(serverSocket);
            serverSocket = -1;
        }

        SocketClearnup();
    }

    bool isRunning() const;

  private:
    bool running = false;

    bool Listen();
    void AcceptConnections();

    SocketFd serverSocket;

    // Maps socket FD → connectionId for sockets in the accumulation phase
    // (before the VC is created and slots are assigned).
    std::unordered_map<SocketFd, uint32_t> socketToConnId;

    // After VC creation: maps clientId → (connectionId → slotIndex).
    // Used to force-close a specific connection by ID during reconnect,
    // bypassing the deadSlots check which may be stale due to half-open TCP.
    std::unordered_map<uint32_t, std::unordered_map<uint32_t, int>> clientConnSlots;
};
