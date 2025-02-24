#include "UdpSocketAddressMap.h"

void UdpSocketAddressMap::setSocketAddress(int socket, sockaddr_in address) {
    addressMap[socket] = address;
}

sockaddr_in UdpSocketAddressMap::getSocketAddress(int socket) {
    return addressMap[socket];
}
