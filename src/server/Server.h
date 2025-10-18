#pragma once
#include <Log.h>
#include <Socket.h>

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
};
