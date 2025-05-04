#include "UdpToTcpSocketMap.h"

void UdpToTcpSocketMap::mapSockets(int udpSocket, int tcpSocket) {
    auto & vec = socketMap[udpSocket];
    for (auto it = vec.begin(); it != vec.end(); ++it) {
        if (*it == tcpSocket) {
            return; // Already mapped
        }
    }
    vec.push_back(tcpSocket);
}

int UdpToTcpSocketMap::retrieveMappedTcpSocket(int udpSocket) {
    auto & vec = socketMap[udpSocket];
    auto & index = lastMappedTcpSocketIndex[udpSocket];
    index = (index) % vec.size();
    return vec[index++];
}
void UdpToTcpSocketMap::Reset() {
  socketMap.clear();
  lastMappedTcpSocketIndex.clear();
}

