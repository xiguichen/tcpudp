#ifndef LOCALUDPSOCKET_H
#define LOCALUDPSOCKET_H

#include <string>
#include <netinet/in.h>

class LocalUdpSocket {
public:
    LocalUdpSocket(int port);
    void bind(int port);
    void send(const std::vector<char>& data);
    std::vector<char> receive();

private:
    int socketFd;
    struct sockaddr_in localAddress;
};

#endif // LOCALUDPSOCKET_H
