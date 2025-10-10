#pragma once
#include <Socket.h>

class Server
{
public:
    Server() = default;
    ~Server() = default;

    void start()
    {
        InitializeSockets();

        // Server logic to start listening for connections would go here
        Listen();

        // Accept connections
        AcceptConnections();
    }

    void stop()
    { 

        CleanupSockets();
    }

    bool isRunning() const;

private:
    bool running = false;

    void Listen();
    void AcceptConnections();

    SocketFd serverSocket;
};
