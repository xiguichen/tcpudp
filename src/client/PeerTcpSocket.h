#ifndef PEERTCPSOCKET_H
#define PEERTCPSOCKET_H

#include "ISocket.h"
#include <Socket.h>
#include <string>
#include <vector>

// Connection state enum
enum class ConnectionState
{
    DISCONNECTED,
    CONNECTING,
    CONNECTED,
    AUTHENTICATED
};

class PeerTcpSocket : public ISocket
{
  public:
    PeerTcpSocket(const std::string &address, int port, uint32_t clientId = 1);

    // ISocket interface implementation
    void connect(const std::string &address, int port) override;
    void send(const std::vector<char> &data) override;
    std::shared_ptr<std::vector<char>> receive() override;
    void close() override;

    // Additional methods specific to PeerTcpSocket
    void sendHandshake();
    void completeHandshake();

    // Destructor
    ~PeerTcpSocket() override;

    // Getters for testing and status
    uint32_t getClientId() const
    {
        return clientId;
    }
    uint32_t getConnectionId() const
    {
        return connectionId;
    }
    ConnectionState getState() const
    {
        return state;
    }
    bool isAuthenticated() const
    {
        return state == ConnectionState::AUTHENTICATED;
    }

  private:
    int socketFd;
    struct sockaddr_in peerAddress;
    uint8_t recvId = 0;
    uint32_t clientId;
    uint32_t connectionId = 0;
    ConnectionState state = ConnectionState::DISCONNECTED;
    bool isConnected = false;
};

#endif // PEERTCPSOCKET_H
