#ifndef UDP_TO_TCP_SOCKET_MAP_H
#define UDP_TO_TCP_SOCKET_MAP_H

#include <map>

class UdpToTcpSocketMap {
public:
    static UdpToTcpSocketMap& getInstance() {
        static UdpToTcpSocketMap instance;
        return instance;
    }

    void mapSockets(int udpSocket, int tcpSocket);
    int retrieveMappedTcpSocket(int udpSocket);

private:
    UdpToTcpSocketMap() = default;
    ~UdpToTcpSocketMap() = default;
    UdpToTcpSocketMap(const UdpToTcpSocketMap&) = delete;
    UdpToTcpSocketMap& operator=(const UdpToTcpSocketMap&) = delete;
private:
    std::map<int, int> socketMap;
};

#endif // UDP_TO_TCP_SOCKET_MAP_H
