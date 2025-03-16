#ifndef LOCALUDPSOCKET_H
#define LOCALUDPSOCKET_H

#include <Socket.h>
#include <vector>
#include <Socket.h>

class LocalUdpSocket {
public:
    LocalUdpSocket(int port);
    void bind(int port);
    void send(const std::vector<char>& data);
    std::vector<char> receive();
    ~LocalUdpSocket();

  private:
    SocketFd socketFd;
    struct sockaddr_in localAddress;
};

#endif // LOCALUDPSOCKET_H
