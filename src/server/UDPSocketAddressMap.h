#ifndef UDP_SOCKET_ADDRESS_MAP_H
#define UDP_SOCKET_ADDRESS_MAP_H

#include <map>
#include <netinet/in.h>

class UDPSocketAddressMap {
public:
    void setSocketAddress(int socket, sockaddr_in address);
    sockaddr_in getSocketAddress(int socket);
private:
    std::map<int, sockaddr_in> addressMap;
};

#endif // UDP_SOCKET_ADDRESS_MAP_H
