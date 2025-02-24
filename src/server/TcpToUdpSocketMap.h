#ifndef TCP_TO_UDP_SOCKET_MAP_H
#define TCP_TO_UDP_SOCKET_MAP_H

#include <map>

class TcpToUdpSocketMap {
public:
    static TcpToUdpSocketMap& getInstance() {
        static TcpToUdpSocketMap instance;
        return instance;
    }

    void mapSockets(int tcpSocket, int udpSocket);
    int retrieveMappedUdpSocket(int tcpSocket);

private:
    TcpToUdpSocketMap() = default;
    ~TcpToUdpSocketMap() = default;
    TcpToUdpSocketMap(const TcpToUdpSocketMap&) = delete;
    TcpToUdpSocketMap& operator=(const TcpToUdpSocketMap&) = delete;

    std::map<int, int> socketMap;
};

#endif // TCP_TO_UDP_SOCKET_MAP_H
