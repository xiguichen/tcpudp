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
    UdpToTcpQueue -> PeerTcpSocket: s2: TCP data 1
    UdpToTcpQueue -> PeerTcpSocket: s2: TCP data 2
    ... More TCP data ...
    UdpToTcpQueue -> PeerTcpSocket: s2: TCP data n
end

group PeerHostReadThread
    note right of PeerTcpSocket
      Ensure thread safety with locks.
      Log errors and exit thread on failure.
    end note
    PeerTcpSocket -> TcpToUdpQueue: s2: TCP data 1
    PeerTcpSocket -> TcpToUdpQueue: s2: TCP data 2
    ... More TCP data ...
    PeerTcpSocket -> TcpToUdpQueue: s2: TCP data n
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

### Class Diagram for client side

@startuml

class Client {
    - localHostUdpPort: int
    - peerTcpPort: int
    - peerAddress: string
    + configure()
}

class SocketManager {
    + manageSockets()
    + cleanupResources()
}

class LocalUdpSocket {
    + bind(port: int)
    + send(data: string)
    + receive(): string
}

class PeerTcpSocket {
    + connect(address: string, port: int)
    + send(data: string)
    + receive(): string
}

class UdpToTcpQueue {
    + enqueue(data: string)
    + dequeue(): string
}

class TcpToUdpQueue {
    + enqueue(data: string)
    + dequeue(): string
}

Client "1" -- "1" SocketManager
SocketManager "1" -- "1" LocalUdpSocket
SocketManager "1" -- "1" PeerTcpSocket
LocalUdpSocket "1" -- "1" UdpToTcpQueue
PeerTcpSocket "1" -- "1" TcpToUdpQueue

@enduml
