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

    // Tracks excess-socket storms: when a client keeps sending connections for
    // a VC that has no dead slots (e.g. because the old VC is a zombie after
    // ReconnectVC closed it but before the server detected the disconnection),
    // the server would reject every connection indefinitely, wasting resources
    // and preventing the new VC from being established.
    //
    // The count resets to zero when the VC is eventually removed.
    struct ExcessTracker
    {
        int count = 0;
        std::chrono::steady_clock::time_point firstSeen;
    };
    std::unordered_map<uint32_t, ExcessTracker> excessByClient;
    static constexpr int EXCESS_LIMIT = 32;    // 32 excess sockets = full VC worth of retries
    static constexpr auto EXCESS_WINDOW = std::chrono::seconds(10);
};
