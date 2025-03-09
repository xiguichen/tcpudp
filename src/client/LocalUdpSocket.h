#ifndef LOCALUDPSOCKET_H
#define LOCALUDPSOCKET_H

#include <string>
#include <netinet/in.h>
#include <vector>

class LocalUdpSocket {
public:
    LocalUdpSocket(int port);
    void bind(int port);
    void send(const std::vector<char>& data);
    std::vector<char> receive();

private:
    int socketFd;
    int sendSocketFd;
    struct sockaddr_in localAddress;
    socklen_t remoteAddressLength;
    struct sockaddr_in remoteAddress;
};

#endif // LOCALUDPSOCKET_H
