#include "UdpSocketAddressMap.h"
#include <cstring> // For memset

void UdpSocketAddressMap::setSocketAddress(int socket, sockaddr_in address) {
    addressMap[socket] = address;
}

sockaddr_in UdpSocketAddressMap::getSocketAddress(int socket) {
    auto it = addressMap.find(socket);
    if (it != addressMap.end()) {
        return it->second;
    }
    
    // Return empty address if not found
    sockaddr_in emptyAddr;
    memset(&emptyAddr, 0, sizeof(emptyAddr));
    return emptyAddr;
}
