#ifndef UDP_TO_TCP_SOCKET_MAP_H
#define UDP_TO_TCP_SOCKET_MAP_H

#include <map>

class UDPToTCPSocketMap {
public:
    static UDPToTCPSocketMap& getInstance() {
        static UDPToTCPSocketMap instance;
        return instance;
    }

    void mapSockets(int udpSocket, int tcpSocket);
    int retrieveMappedTCPSocket(int udpSocket);

private:
    UDPToTCPSocketMap() = default;
    ~UDPToTCPSocketMap() = default;
    UDPToTCPSocketMap(const UDPToTCPSocketMap&) = delete;
    UDPToTCPSocketMap& operator=(const UDPToTCPSocketMap&) = delete;
private:
    std::map<int, int> socketMap;
};

#endif // UDP_TO_TCP_SOCKET_MAP_H
