## Server Side Revert UDP data

### Connection Management Thread

@startuml
participant ClientPeer1
participant ClientPeer2
participant SocketManager
participant ReaderThread1
participant ReaderThread2
participant WriterThread1
participant WriterThread2

SocketManager -> SocketManager: Listen for incoming TCP connections

ClientPeer1 -> SocketManager: TCP connection request
SocketManager -> ClientPeer1: Accept connection
SocketManager -> ReaderThread1: Start new reader thread for ClientPeer1
SocketManager -> WriterThread1: Start new write thread for ClientPeer1

ClientPeer2 -> SocketManager: TCP connection request
SocketManager -> ClientPeer2: Accept connection
SocketManager -> ReaderThread2: Start new reader thread for ClientPeer2
SocketManager -> WriterThread2: Start new write thread for ClientPeer2

group ReaderThread1
    ReaderThread1 -> ClientPeer1: Handle data from ClientPeer1
    ... More data handling ...
end

group ReaderThread2
    ReaderThread2 -> ClientPeer2: Handle data from ClientPeer2
    ... More data handling ...
end

group WriterThread1
    WriterThread1 -> ClientPeer1: Send data to ClientPeer1
    ... More data sending ...
end

group WriterThread2
    WriterThread2 -> ClientPeer2: Send data to ClientPeer2
    ... More data sending ...
end

@enduml


### Reader thread for TCP data
@startuml

participant TCPSocket
queue TCPDataQueue

group TCPDataReaderThread
    loop Read from socket
        TCPSocket -> TCPSocket: Read data length
        TCPSocket -> TCPSocket: Read data based on length
        TCPSocket -> TCPDataQueue: Enqueue complete data packet with socket information
    end
end

@enduml


### Writer thread for handling TCP queue data


@startuml
participant UDPSocket
queue TCPDataQueue
collections TCPToUDPSocketMap
collections UDPToTCPSocketMap

group TCPQueueWriterThread
    loop Process data
        TCPDataQueue -> TCPQueueWriterThread: Dequeue data
        TCPQueueWriterThread -> TCPQueueWriterThread: Extract socket and data
        alt Socket exists in TCPToUDPSocketMap
            TCPQueueWriterThread -> TCPToUDPSocketMap: Retrieve mapped UDP socket
            TCPQueueWriterThread -> UDPSocket: Use mapped UDP socket to send data via UDP
        else
            TCPQueueWriterThread -> UDPSocket: Create new UDP socket (udpSocket)
            TCPQueueWriterThread -> TCPToUDPSocketMap: Map new UDP socket with existing TCP socket
            TCPQueueWriterThread -> UDPToTCPSocketMap: Map new UDP socket to existing TCP socket
            TCPQueueWriterThread -> UDPSocket: Use created UDP socket to send data via UDP
            TCPQueueWriterThread -> TCPQueueWriterThread: Start UDPReaderThread for udpSocket
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
