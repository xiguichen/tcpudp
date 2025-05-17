#ifndef UDP_TO_TCP_SOCKET_MAP_H
#define UDP_TO_TCP_SOCKET_MAP_H

#include <unordered_map>
#include <vector>

class UdpToTcpSocketMap {
public:
    static UdpToTcpSocketMap& getInstance() {
        static UdpToTcpSocketMap instance;
        return instance;
    }

    void mapSockets(int udpSocket, int tcpSocket);
    int retrieveMappedTcpSocket(int udpSocket);
    void Reset();

private:
    UdpToTcpSocketMap() {
        // Reserve space for expected number of connections
        socketMap.reserve(1000);
        lastMappedTcpSocketIndex.reserve(1000);
    }
    ~UdpToTcpSocketMap() = default;
    UdpToTcpSocketMap(const UdpToTcpSocketMap&) = delete;
    UdpToTcpSocketMap& operator=(const UdpToTcpSocketMap&) = delete;
    std::unordered_map<int, std::vector<int>> socketMap;
    std::unordered_map<int, int> lastMappedTcpSocketIndex;
};

#endif // UDP_TO_TCP_SOCKET_MAP_H
