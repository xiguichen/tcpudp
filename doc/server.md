## Server Side Revert UDP data

### Connection Management Thread

@startuml
participant ClientPeer1
participant ClientPeer2
participant SocketManager
participant SocketMap
participant TcpToQueueThread1
participant TcpToQueueThread2
participant UdpToQueueThread1
participant UdpToQueueThread2

SocketManager -> SocketManager: Create socket and bind to port 5002
SocketManager -> SocketManager: Listen for incoming TCP connections

ClientPeer1 -> SocketManager: TCP connection request
SocketManager -> ClientPeer1: Accept connection
SocketManager -> SocketManager: Create a new socket for UDP connection
SocketManager -> SocketMap: Map TCP socket and UDP socket
SocketManager -> TcpToQueueThread1: Start new thread for ClientPeer1
SocketManager -> UdpToQueueThread1: Start new thread for ClientPeer1

ClientPeer2 -> SocketManager: TCP connection request
SocketManager -> ClientPeer2: Accept connection
SocketManager -> SocketManager: Create a new socket for UDP connection
SocketManager -> SocketMap: Map TCP socket and UDP socket
SocketManager -> TcpToQueueThread2: Start new thread for ClientPeer2
SocketManager -> UdpToQueueThread2: Start new thread for ClientPeer2

@enduml

### Reader

#### TcpToQueueThread for reading data from TCP socket
@startuml

participant TcpSocket
queue TcpDataQueue

group TcpToQueueThread
    loop Read from socket
        TcpSocket -> TcpSocket: Read data length
        TcpSocket -> TcpSocket: Read data based on length
        TcpSocket -> TcpDataQueue: Enqueue complete data packet with socket information
    end
end

@enduml


#### UdpToQueueThread for reading data from UDP socket

@startuml

participant UdpSocket
queue UdpDataQueue
collections UdpSocketAddressMap

group UdpToQueueThread
    loop Read from UDP socket
        UdpSocket -> UdpSocket: Read data
        UdpSocket -> UdpSocketAddressMap: set socket address
        UdpSocket -> UdpDataQueue: Enqueue received data with socket information
    end
end

@enduml


### Writer

#### TcpQueueToUdpThreadPool for sending data from TCP to UDP

@startuml
participant UdpSocket
queue TcpDataQueue
collections TcpToUdpSocketMap
collections UdpSocketAddressMap

group TcpQueueToUdpThreadPool
    loop Process data
        TcpDataQueue -> TcpQueueToUdpThread: Dequeue data
        TcpQueueToUdpThread -> TcpQueueToUdpThread: Extract socket and data
        TcpQueueToUdpThread -> UdpSocketAddressMap: Retrieve socket address
        TcpQueueToUdpThread -> TcpToUdpSocketMap: Retrieve mapped UDP socket
        TcpQueueToUdpThread -> UdpSocket: Use mapped UDP socket to send data via UDP
    end
end

@enduml


#### UdpQueueToTcpThreadPool for sending data from UDP to TCP

@startuml
participant TcpSocket
queue UdpDataQueue
collections UdpToTcpSocketMap

group UdpQueueToTcpThreadPool
    loop Process data concurrently
        UdpDataQueue -> UdpQueueToTcpThreadPool: Dequeue data
        UdpQueueToTcpThreadPool -> UdpQueueToTcpThreadPool: Extract socket and data
        UdpQueueToTcpThreadPool -> UdpToTcpSocketMap: Retrieve mapped TCP socket
        UdpQueueToTcpThreadPool -> TcpSocket: Use mapped TCP socket to send data via TCP in the format of {length, data}
    end
end

@enduml


### Class Diagram

@startuml Server Classes

' Socket Manager
class SocketManager {
  - serverSocket: int
  - threads: vector<thread>
  + SocketManager()
  + ~SocketManager()
  + createSocket(): void
  + bindToPort(port: int): void
  + listenForConnections(): void
  + acceptConnection(): void
  + startTcpToQueueThread(clientSocket: int): void
  + startUdpToQueueThread(clientSocket: int): void
  + startTcpQueueToUdpThreadPool(): void
  + startUdpQueueToTcpThreadPool(): void
}

' Socket Maps
class TcpToUdpSocketMap {
  - socketMap: map<int, int>
  - TcpToUdpSocketMap()
  - ~TcpToUdpSocketMap()
  + {static} getInstance(): TcpToUdpSocketMap&
  + mapSockets(tcpSocket: int, udpSocket: int): void
  + retrieveMappedUdpSocket(tcpSocket: int): int
}

class UdpToTcpSocketMap {
  - socketMap: map<int, vector<int>>
  - lastMappedTcpSocketIndex: map<int, int>
  - UdpToTcpSocketMap()
  - ~UdpToTcpSocketMap()
  + {static} getInstance(): UdpToTcpSocketMap&
  + mapSockets(udpSocket: int, tcpSocket: int): void
  + retrieveMappedTcpSocket(udpSocket: int): int
  + Reset(): void
}

' Data Queues
class TcpDataQueue {
  - queue: queue<pair<int, shared_ptr<vector<char>>>>
  - queueMutex: mutex
  - cv: condition_variable
  - TcpDataQueue()
  - ~TcpDataQueue()
  + {static} getInstance(): TcpDataQueue&
  + enqueue(socket: int, data: shared_ptr<vector<char>>): void
  + dequeue(): pair<int, shared_ptr<vector<char>>>
}

class UdpDataQueue {
  - queue: queue<pair<int, shared_ptr<vector<char>>>>
  - queueMutex: mutex
  - cv: condition_variable
  - sendId: uint8_t
  - lastEmitTime: chrono::time_point
  - bufferedNewDataMap: map<int, vector<char>>
  - UdpDataQueue()
  - ~UdpDataQueue()
  - enqueueAndNotify(socket: int, data: shared_ptr<vector<char>>, bufferedNewData: vector<char>&): void
  + {static} getInstance(): UdpDataQueue&
  + enqueue(socket: int, data: shared_ptr<vector<char>>): void
  + dequeue(): pair<int, shared_ptr<vector<char>>>
}

' Thread Classes
class TcpToQueueThread {
  - socket_: int
  - readFromSocket(buffer: char*, bufferSize: size_t): size_t
  - enqueueData(data: const char*, length: size_t): void
  + TcpToQueueThread(socket: int)
  + run(): void
}

class UdpToQueueThread {
  - socket_: int
  - readFromUdpSocket(buffer: char*, bufferSize: size_t): size_t
  - enqueueData(data: char*, length: size_t): void
  + UdpToQueueThread(socket: int)
  + run(): void
}

class TcpQueueToUdpThreadPool {
  - processData(): void
  - sendDataViaUdp(socket: int, data: shared_ptr<vector<char>>): void
  + run(): void
}

class UdpQueueToTcpThreadPool {
  - processDataConcurrently(): void
  - sendDataViaTcp(tcpSocket: int, data: const vector<char>&): void
  + run(): void
}

' Configuration
class Configuration {
  - {static} instance: Configuration*
  - allowedClientIds: set<uint32_t>
  - Configuration()
  + {static} getInstance(): Configuration*
  + getSocketAddress(): string
  + getPortNumber(): int
  + getAllowedClientIds(): const set<uint32_t>&
}

' UDP Socket Address Map
class UdpSocketAddressMap {
  - addressMap: map<int, sockaddr_in>
  + setSocketAddress(socket: int, address: sockaddr_in): void
  + getSocketAddress(socket: int): sockaddr_in
}

' Key Relationships with Cardinality

' SocketManager creates and manages threads
SocketManager "1" o--> "*" TcpToQueueThread : creates >
SocketManager "1" o--> "*" UdpToQueueThread : creates >
SocketManager "1" o--> "*" TcpQueueToUdpThreadPool : creates >
SocketManager "1" o--> "*" UdpQueueToTcpThreadPool : creates >

' SocketManager uses socket maps
SocketManager "1" --> "1" TcpToUdpSocketMap : uses >
SocketManager "1" --> "1" UdpToTcpSocketMap : uses >

' Thread classes use data queues
TcpToQueueThread "*" --> "1" TcpDataQueue : enqueues data >
UdpToQueueThread "*" --> "1" UdpDataQueue : enqueues data >
TcpQueueToUdpThreadPool "*" --> "1" TcpDataQueue : dequeues data >
UdpQueueToTcpThreadPool "*" --> "1" UdpDataQueue : dequeues data >

' Thread pools use socket maps
TcpQueueToUdpThreadPool "*" --> "1" TcpToUdpSocketMap : looks up UDP socket >
UdpQueueToTcpThreadPool "*" --> "1" UdpToTcpSocketMap : looks up TCP socket >

' Socket maps store mappings
TcpToUdpSocketMap "1" o--> "*" "TCP-UDP Mapping" : contains >
UdpToTcpSocketMap "1" o--> "*" "UDP-TCP Mapping" : contains >

' UdpToQueueThread uses UdpSocketAddressMap
UdpToQueueThread "*" --> "1" UdpSocketAddressMap : uses >

' SocketManager uses Configuration
SocketManager "1" --> "1" Configuration : uses >

' Singleton pattern notation
TcpToUdpSocketMap -[hidden]-> UdpToTcpSocketMap
TcpDataQueue -[hidden]-> UdpDataQueue
note "Singleton Pattern" as SingletonNote
TcpToUdpSocketMap .. SingletonNote
UdpToTcpSocketMap .. SingletonNote
TcpDataQueue .. SingletonNote
UdpDataQueue .. SingletonNote
Configuration .. SingletonNote

@enduml


### Error Handling
- Define specific error handling strategies for socket errors, data corruption, and missing mappings.

### Concurrency Management
- Use locks to make sure that shared data structures are accessed safely by multiple threads.

### Configuration and Initialization
- Use JSON configuration files to set up the server and initialize sockets and queues.

### Lifecycle Management
- SocketManager should handle the lifecycle of reader and writer threads, including starting, stopping, and restarting them.

### Logging and Monitoring
- All the logs should be written to a log file and could be printed to the console at the same time. Errors should be logged with a timestamp and a description of the error.

### Security Considerations
- In phase 1, we don't need to consider security. In phase 2, we will need to implement encryption and authentication mechanisms.

### Performance Considerations
- We will use std::vector<char> to store data packets in queues to minimize memory allocation overhead.

### Testing Strategy
- Unit test project is needed to test the server's functionality and performance. We will use Google Test for unit testing.
- Manual testing is also needed to test the server's functionality and performance.

