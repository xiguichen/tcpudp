## Server Side Revert UDP data

### Connection Management Thread

@startuml
participant ClientPeer1
participant ClientPeer2
participant SocketManager
participant TCPSocketReaderThread1
participant TCPSocketReaderThread2
participant WriterThread1
participant WriterThread2

SocketManager -> SocketManager: Listen for incoming TCP connections

ClientPeer1 -> SocketManager: TCP connection request
SocketManager -> ClientPeer1: Accept connection
SocketManager -> TCPSocketReaderThread1: Start new reader thread for ClientPeer1
SocketManager -> WriterThread1: Start new write thread for ClientPeer1

ClientPeer2 -> SocketManager: TCP connection request
SocketManager -> ClientPeer2: Accept connection
SocketManager -> TCPSocketReaderThread2: Start new reader thread for ClientPeer2
SocketManager -> WriterThread2: Start new write thread for ClientPeer2

@enduml

### Reader thread for read data from TCP socket
@startuml

participant TCPSocket
queue TCPDataQueue

group TCPSocketReaderThread
    loop Read from socket
        TCPSocket -> TCPSocket: Read data length
        TCPSocket -> TCPSocket: Read data based on length
        TCPSocket -> TCPDataQueue: Enqueue complete data packet with socket information
    end
end

@enduml

### Thread for send data received from TCP socket to UDP socket

@startuml
participant UDPSocket
queue TCPDataQueue
collections TCPToUDPSocketMap
collections UDPToTCPSocketMap

group TcpQueueToUdpSocketThread
    loop Process data
        TCPDataQueue -> TcpQueueToUdpSocketThread: Dequeue data
        TcpQueueToUdpSocketThread -> TcpQueueToUdpSocketThread: Extract socket and data
        alt Socket exists in TCPToUDPSocketMap
            TcpQueueToUdpSocketThread -> TCPToUDPSocketMap: Retrieve mapped UDP socket
            TcpQueueToUdpSocketThread -> UDPSocket: Use mapped UDP socket to send data via UDP
        else
            TcpQueueToUdpSocketThread -> UDPSocket: Create new UDP socket (udpSocket)
            TcpQueueToUdpSocketThread -> TCPToUDPSocketMap: Map new UDP socket with existing TCP socket
            TcpQueueToUdpSocketThread -> UDPToTCPSocketMap: Map new UDP socket to existing TCP socket
            TcpQueueToUdpSocketThread -> UDPSocket: Use created UDP socket to send data via UDP
            TcpQueueToUdpSocketThread -> TcpQueueToUdpSocketThread: Start UDPReaderThread for udpSocket
        end
    end
end

@enduml

### Reader thread for read UDP data

@startuml

participant UDPSocket
queue UDPDataQueue

group UDPDataReaderThread
    loop Read from UDP socket
        UDPSocket -> UDPSocket: Read data
        UDPSocket -> UDPDataQueue: Enqueue received data with socket information
    end
end

@enduml

### Writer thread pool for handling UDP queue data

@startuml
participant UDPSocket
queue UDPDataQueue
collections UDPToTCPSocketMap

group UDPQueueWriterThreadPool
    loop Process data concurrently
        UDPDataQueue -> UDPQueueWriterThreadPool: Dequeue data
        UDPQueueWriterThreadPool -> UDPQueueWriterThreadPool: Extract socket and data
        alt Socket exists in UDPToTCPSocketMap
            UDPQueueWriterThreadPool -> UDPToTCPSocketMap: Retrieve mapped TCP socket
            UDPQueueWriterThreadPool -> UDPSocket: Use mapped TCP socket to send data via TCP in the format of {length, data}
        else
            UDPQueueWriterThreadPool -> UDPQueueWriterThreadPool: Log error or handle missing mapping
        end
    end
end

@enduml

### Class and Method Definitions
- **SocketManager**: Manages incoming TCP connections and starts reader and writer threads.
- **ReaderThread**: Handles reading data from a socket and enqueuing it.
- **WriterThread**: Handles dequeuing data and sending it to the appropriate socket.

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

### Class Diagram
@startuml

class SocketManager {
    +void listenForConnections()
    +void acceptConnection(ClientPeer)
    +void startReaderThread(ClientPeer)
    +void startWriterThread(ClientPeer)
}

class ReaderThread {
    +void handleData(ClientPeer)
}

class WriterThread {
    +void sendData(ClientPeer)
}

class TCPSocket {
    +int readDataLength()
    +char[] readData(int length)
}

class TCPDataQueue {
    +void enqueueData(char[] data, TCPSocket socket)
    +char[] dequeueData()
}

class UDPSocket {
    +void sendData(char[] data)
    +char[] readData()
}

class UDPDataQueue {
    +void enqueueData(char[] data, UDPSocket socket)
    +char[] dequeueData()
}

class TCPToUDPSocketMap {
    +UDPSocket getMappedUDPSocket(TCPSocket socket)
    +void mapSockets(TCPSocket tcpSocket, UDPSocket udpSocket)
}

class UDPToTCPSocketMap {
    +TCPSocket getMappedTCPSocket(UDPSocket socket)
    +void mapSockets(UDPSocket udpSocket, TCPSocket tcpSocket)
}

SocketManager -> ReaderThread
SocketManager -> WriterThread
ReaderThread -> TCPDataQueue
WriterThread -> TCPDataQueue
WriterThread -> UDPSocket
UDPSocket -> UDPDataQueue
UDPDataQueue -> UDPToTCPSocketMap
UDPToTCPSocketMap -> TCPSocket

@enduml
