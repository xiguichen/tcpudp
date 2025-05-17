#include "UdpToTcpSocketMap.h"
#include <algorithm> // For std::find

void UdpToTcpSocketMap::mapSockets(int udpSocket, int tcpSocket) {
    auto & vec = socketMap[udpSocket];
    // Check if already mapped - use find instead of iterating
    if (std::find(vec.begin(), vec.end(), tcpSocket) == vec.end()) {
        vec.push_back(tcpSocket);
    }
}

int UdpToTcpSocketMap::retrieveMappedTcpSocket(int udpSocket) {
    auto it = socketMap.find(udpSocket);
    if (it == socketMap.end() || it->second.empty()) {
        return -1; // No mapping found
    }
    
    auto & vec = it->second;
    auto & index = lastMappedTcpSocketIndex[udpSocket];
    
    // Make sure index is valid
    if (vec.empty()) {
        return -1;
    }
    
    index = (index) % vec.size();
    return vec[index++];
}

void UdpToTcpSocketMap::Reset() {
    socketMap.clear();
    lastMappedTcpSocketIndex.clear();
}

