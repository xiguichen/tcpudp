#ifndef PEERTCPSOCKET_H
#define PEERTCPSOCKET_H

#include <string>
#include <vector>
#include <Socket.h>

class PeerTcpSocket {
public:
    PeerTcpSocket(const std::string& address, int port);
    void connect(const std::string& address, int port);
    void send(const std::vector<char>& data);
    void sendHandshake();
    std::vector<char> receive();
    ~PeerTcpSocket();

  private:
    int socketFd;
    struct sockaddr_in peerAddress;
    uint8_t recvId = 0;
};

#endif // PEERTCPSOCKET_H
