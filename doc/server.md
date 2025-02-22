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

participant TCPSocket
queue TCPDataQueue

group TcpToQueueThread
    loop Read from socket
        TCPSocket -> TCPSocket: Read data length
        TCPSocket -> TCPSocket: Read data based on length
        TCPSocket -> TCPDataQueue: Enqueue complete data packet with socket information
    end
end

@enduml


#### UdpToQueueThread for reading data from UDP socket

@startuml

participant UDPSocket
queue UDPDataQueue
collections UDPSocketAddressMap

group UdpToQueueThread
    loop Read from UDP socket
        UDPSocket -> UDPSocket: Read data
        UDPSocket -> UDPSocketAddressMap: set socket address
        UDPSocket -> UDPDataQueue: Enqueue received data with socket information
    end
end

@enduml


### Writer

#### TcpToUdpQueueThreadPool for sending data from TCP to UDP

@startuml
participant UDPSocket
queue TCPDataQueue
collections TCPToUDPSocketMap
collections UDPSocketAddressMap

group TCPQueueToUDPThreadPool
    loop Process data
        TCPDataQueue -> TCPQueueToUDPThread: Dequeue data
        TCPQueueToUDPThread -> TCPQueueToUDPThread: Extract socket and data
        TCPQueueToUDPThread -> UDPSocketAddressMap: Retrieve socket address
        TCPQueueToUDPThread -> TCPToUDPSocketMap: Retrieve mapped UDP socket
        TCPQueueToUDPThread -> UDPSocket: Use mapped UDP socket to send data via UDP
    end
end

@enduml


#### UdpQueueToTcpThreadPool for sending data from UDP to TCP

@startuml
participant TCPSocket
queue UDPDataQueue
collections UDPToTCPSocketMap

group UdpQueueToTcpThreadPool
    loop Process data concurrently
        UDPDataQueue -> UdpQueueToTcpThreadPool: Dequeue data
        UdpQueueToTcpThreadPool -> UdpQueueToTcpThreadPool: Extract socket and data
        UdpQueueToTcpThreadPool -> UDPToTCPSocketMap: Retrieve mapped TCP socket
        UdpQueueToTcpThreadPool -> TCPSocket: Use mapped TCP socket to send data via TCP in the format of {length, data}
    end
end

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

