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

SocketManager -> SocketManager: Create socket and bind to port 5001
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

@startuml

class SocketManager {
    +createSocket()
    +bindToPort(port: int)
    +listenForConnections()
    +acceptConnection()
    +createUdpSocket()
    +mapSockets()
    +startTcpToQueueThread()
    +startUdpToQueueThread()
}

class SocketMap {
    +mapTcpSocketToUdpSocket(tcpSocket, udpSocket)
    +getMappedUdpSocket(tcpSocket)
    +getMappedTcpSocket(udpSocket)
}

class TcpToQueueThread {
    +run()
    -readFromSocket()
    -enqueueData()
}

class UdpToQueueThread {
    +run()
    -readFromUdpSocket()
    -enqueueData()
}

class TcpQueueToUdpThreadPool {
    +run()
    -processData()
    -sendDataViaUdp()
}

class UdpQueueToTcpThreadPool {
    +run()
    -processDataConcurrently()
    -sendDataViaTcp()
}

class TcpDataQueue {
    +enqueue(data)
    +dequeue()
}

class UdpDataQueue {
    +enqueue(data)
    +dequeue()
}

class UdpSocketAddressMap {
    +setSocketAddress()
    +getSocketAddress()
}

class TcpToUdpSocketMap {
    +retrieveMappedUdpSocket()
}

class UdpToTcpSocketMap {
    +retrieveMappedTcpSocket()
}

SocketManager -> SocketMap
SocketManager -> TcpToQueueThread
SocketManager -> UdpToQueueThread
TcpToQueueThread -> TcpDataQueue
UdpToQueueThread -> UdpDataQueue
TcpQueueToUdpThreadPool -> TcpDataQueue
TcpQueueToUdpThreadPool -> UdpSocketAddressMap
TcpQueueToUdpThreadPool -> TcpToUdpSocketMap
UdpQueueToTcpThreadPool -> UdpDataQueue
UdpQueueToTcpThreadPool -> UdpToTcpSocketMap

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

