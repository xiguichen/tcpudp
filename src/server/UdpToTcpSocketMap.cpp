#include "UdpToTcpSocketMap.h"
#include <algorithm> // For std::find
#include <Log.h>
#include <format>

using namespace Logger;

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
    Log::getInstance().info(std::format("use the socket at index: {}", index));
    return vec[index++];
}

void UdpToTcpSocketMap::Reset() {
    socketMap.clear();
    lastMappedTcpSocketIndex.clear();
}

