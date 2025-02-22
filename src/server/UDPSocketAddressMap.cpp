#include "UDPSocketAddressMap.h"

void UDPSocketAddressMap::setSocketAddress(int socket, sockaddr_in address) {
    addressMap[socket] = address;
}

sockaddr_in UDPSocketAddressMap::getSocketAddress(int socket) {
    return addressMap[socket];
}
