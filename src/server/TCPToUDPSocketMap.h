#ifndef TCP_TO_UDP_SOCKET_MAP_H
#define TCP_TO_UDP_SOCKET_MAP_H

#include <map>

class TCPToUDPSocketMap {
public:
    static TCPToUDPSocketMap& getInstance() {
        static TCPToUDPSocketMap instance;
        return instance;
    }

    void mapSockets(int tcpSocket, int udpSocket);
    int retrieveMappedUDPSocket(int tcpSocket);

private:
    TCPToUDPSocketMap() = default;
    ~TCPToUDPSocketMap() = default;
    TCPToUDPSocketMap(const TCPToUDPSocketMap&) = delete;
    TCPToUDPSocketMap& operator=(const TCPToUDPSocketMap&) = delete;

    std::map<int, int> socketMap;
};

#endif // TCP_TO_UDP_SOCKET_MAP_H
