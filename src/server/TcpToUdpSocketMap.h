#ifndef TCP_TO_UDP_SOCKET_MAP_H
#define TCP_TO_UDP_SOCKET_MAP_H

#include <unordered_map>

class TcpToUdpSocketMap {
public:
    static TcpToUdpSocketMap& getInstance() {
        static TcpToUdpSocketMap instance;
        return instance;
    }

    void mapSockets(int tcpSocket, int udpSocket);
    int retrieveMappedUdpSocket(int tcpSocket);

private:
    TcpToUdpSocketMap() {
        // Reserve space for expected number of connections
        socketMap.reserve(1000);
    }
    ~TcpToUdpSocketMap() = default;
    TcpToUdpSocketMap(const TcpToUdpSocketMap&) = delete;
    TcpToUdpSocketMap& operator=(const TcpToUdpSocketMap&) = delete;

    std::unordered_map<int, int> socketMap;
};

#endif // TCP_TO_UDP_SOCKET_MAP_H
