#ifndef UDP_TO_TCP_SOCKET_MAP_H
#define UDP_TO_TCP_SOCKET_MAP_H

#include <map>
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
    UdpToTcpSocketMap() = default;
    ~UdpToTcpSocketMap() = default;
    UdpToTcpSocketMap(const UdpToTcpSocketMap&) = delete;
    UdpToTcpSocketMap& operator=(const UdpToTcpSocketMap&) = delete;
    std::map<int, std::vector<int>> socketMap;
    std::map<int, int> lastMappedTcpSocketIndex;
};

#endif // UDP_TO_TCP_SOCKET_MAP_H
