#ifndef UDP_SOCKET_ADDRESS_MAP_H
#define UDP_SOCKET_ADDRESS_MAP_H

#include <unordered_map>
#include <Socket.h>

class UdpSocketAddressMap {
public:
    UdpSocketAddressMap() {
        // Reserve space for expected number of connections
        addressMap.reserve(1000);
    }
    
    void setSocketAddress(int socket, sockaddr_in address);
    sockaddr_in getSocketAddress(int socket);
private:
    std::unordered_map<int, sockaddr_in> addressMap;
};

#endif // UDP_SOCKET_ADDRESS_MAP_H
