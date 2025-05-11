## Configuration

### Client Configuration File

```json
{
  "localHostUdpPort": 5002,
  "peerTcpPort": 6001,
  "peerAddress": "peer address"
}

```

## Client Side (Behind NAT)

@startuml

participant Client 
participant SocketManager
participant LocalUdpSocket
queue UdpToTcpQueue
participant PeerTcpSocket
queue TcpToUdpQueue

note right of LocalUdpSocket
  LocalUdpSocket is configurable via the config file.
end note

SocketManager -> LocalUdpSocket: Create socket s1 and bind s1 to port 5002

group LocalHostReadThread
    note right of LocalUdpSocket
      Ensure thread safety with locks.
      Log errors and exit thread on failure.
    end note
    Client -> LocalUdpSocket : UDP data 1
    LocalUdpSocket -> UdpToTcpQueue: s1 : UDP data 1
    Client -> LocalUdpSocket : UDP data 2
    LocalUdpSocket -> UdpToTcpQueue: s1: UDP data 2
    ... More UDP data ...
    Client -> LocalUdpSocket : UDP data n
    LocalUdpSocket -> UdpToTcpQueue: s1: UDP data n
end

note right of UdpToTcpQueue
  UdpToTcpQueue is thread-safe to handle concurrent access.
end note

SocketManager -> PeerTcpSocket: Create socket s2 and connect to peer port 6001

note right of PeerTcpSocket
  Peer address and port are configurable via the config file.
end note

group LocalHostWriteThread
    note right of PeerTcpSocket
      Ensure thread safety with locks.
      Log errors and exit thread on failure.
    end note
    UdpToTcpQueue -> PeerTcpSocket: s2: TCP data 1 [Length, Data]
    UdpToTcpQueue -> PeerTcpSocket: s2: TCP data 2 [Length, Data]
    ... More TCP data ...
    UdpToTcpQueue -> PeerTcpSocket: s2: TCP data n [Length, Data]
end

group PeerHostReadThread
    note right of PeerTcpSocket
      Ensure thread safety with locks.
      Log errors and exit thread on failure.
    end note
    PeerTcpSocket -> TcpToUdpQueue: s2: TCP data 1 [Length, Data]
    PeerTcpSocket -> TcpToUdpQueue: s2: TCP data 2 [Length, Data]
    ... More TCP data ...
    PeerTcpSocket -> TcpToUdpQueue: s2: TCP data n [Length, Data]
end

group PeerHostWriteThread
    note right of TcpToUdpQueue
      Ensure thread safety with locks.
      Log errors and exit thread on failure.
    end note
    TcpToUdpQueue -> Client: s1: UDP data 1
    TcpToUdpQueue -> Client: s1: UDP data 2
    ... More UDP data ...
    TcpToUdpQueue -> Client: s1: UDP data n
end


note right of SocketManager
  Each resource is managed by a manager class.
  Resources are cleaned up at program exit.
end note
@enduml



### class diagram

@startuml

class Client {
    - int localHostUdpPort
    - int peerTcpPort
    - uint32_t clientId
    - std::string peerAddress
    - std::shared_ptr<SocketManager> socketManager
    + Client(const std::string& configFile)
    + void configure()
    - void loadConfig(const std::string& configFile)
}

class LocalUdpSocket {
    - SocketFd socketFd
    - struct sockaddr_in localAddress
    - bool bClosed
    + LocalUdpSocket(int port)
    + void bind(int port)
    + void send(const std::vector<char>& data)
    + std::vector<char> receive()
    + void close()
    + ~LocalUdpSocket()
}

class PeerTcpSocket {
    - int socketFd
    - struct sockaddr_in peerAddress
    - uint8_t recvId
    + PeerTcpSocket(const std::string& address, int port)
    + void connect(const std::string& address, int port)
    + void send(const std::vector<char>& data)
    + void sendHandshake()
    + std::vector<char> receive()
    + ~PeerTcpSocket()
}

class SocketManager {
    - LocalUdpSocket localUdpSocket
    - PeerTcpSocket peerTcpSocket
    - UdpToTcpQueue udpToTcpQueue
    - TcpToUdpQueue tcpToUdpQueue
    + SocketManager(int udpPort, int tcpPort, const std::string& address)
    + void manageSockets()
    + void cleanupResources()
    - void localHostReadTask(bool& running)
    - void localHostWriteTask(bool& running)
    - void peerHostReadTask(bool& running)
    - void peerHostWriteTask(bool& running)
    + ~SocketManager()
}

class TcpToUdpQueue {
    - std::queue<std::vector<char>> queue
    - std::mutex mtx
    - std::condition_variable cv
    - bool shouldCancel
    + void enqueue(const std::vector<char>& data)
    + std::vector<char> dequeue()
    + void cancel()
}

class UdpToTcpQueue {
    - std::queue<std::vector<char>> queue
    - std::mutex mtx
    - std::condition_variable cv
    - bool shouldCancel
    - std::chrono::time_point<std::chrono::high_resolution_clock> lastEmitTime
    - std::vector<char> bufferedNewData
    - uint8_t sendId
    + void enqueue(const std::vector<char>& data)
    + std::vector<char> dequeue()
    + void cancel()
    - void enqueueAndNotify(const std::vector<char>& data, std::vector<char>& bufferedNewData)
}

Client --> SocketManager
SocketManager --> LocalUdpSocket
SocketManager --> PeerTcpSocket
SocketManager --> UdpToTcpQueue
SocketManager --> TcpToUdpQueue

@enduml
