### Data Processing System Design


```plantuml

class StopableThread {
    +StopableThread()
    +~StopableThread()
    +start()
    +stop()
    ..protected..
     #run()
    #isRunning() : bool
    #setRunning(running : bool)
    ..private..
    -_thread : std::thread
    -running : bool
}


class IDataProcessor {
    +IDataProcessor()
    +processData(data : char*, size : size_t)
}

class TcpDataReader extends IDataReader {
    +TcpDataReader(socketFd : SocketFd)
    +readData() std::shared_ptr<std::vector<char>>
    +hasMoreData() : bool
    ..private..
    -_socketFd: SocketFd
}

class QueueDataReader extends IDataReader {
    +QueueDataReader(socketFd : SocketFd)
    +readData() std::shared_ptr<std::vector<char>>
    +hasMoreData() : bool
    ..private..
    -_queue: BlockingQueue
}

class IDataReader {
    +IDataReader()
    +readData() std::shared_ptr<std::vector<char>>
    +hasMoreData() : bool
}

class DataProcessorThread extends StopableThread {
    +DataProcessorThread()
    +run()
    ..private..
     -_dataReader : IDataReader
    -_dataProcessor : IDataProcessor
}


DataProcessorThread *-- IDataReader
DataProcessorThread *-- IDataProcessor


```

### VC Data Structure

```plantuml


struct VcHeader
{
    uint16_t size;
    uint16_t type;
}

struct VcData
{
    VcHeader header;
    uint32_t frameId;
    char* data;
}

struct VcDataAck
{
    VcHeader header;
    uint32_t frameId;
}

VcData *-> VcHeader
VcDataAck *-> VcHeader


```

### VC Data Processing threads

#### Send Data to remote

```plantuml


component "Socket 1" as Socket1 {
}

() "Socket Write" as SocketWrite


() "Data Ack Callback" as DataAckCallback

component "DataWriteThread" as DataWriteThread {

}

component "DataReadThread" as DataReadThread {
}


component "Socket 2" as Socket2 {
}
() "Socket Read" as SocketRead
Socket2 -- SocketRead

database "UnAcked Data Map" as UnAckedDataMap {
}
() "Map Add" as MapAdd
() "Map Remove" as MapRemove

UnAckedDataMap - MapAdd
MapRemove -d- UnAckedDataMap


SocketWrite -u- Socket1
DataWriteThread -u-> SocketWrite: "1. Data 1\n 6. Data 2"
DataWriteThread -l-> MapAdd : "2. Data 1, wait for Ack 1"
DataReadThread -u-> SocketRead : "3. Ack 1"
DataReadThread -d-> DataAckCallback : "4. Ack 1"
DataAckCallback -> MapRemove : "5. Data 1"

```

### Receive data from remote

```plantuml

component "Socket 1" as Socket1 {
}

() "Socket Read" as SocketRead
SocketRead -u- Socket1

component "DataReadThread" as DataReadThread {
}

() "Parse Data" as ParseData
DataReadThread -- ParseData

component "DataWriteThread" as DataWriteThread {
}

DataReadThread -u-> SocketRead : "1. Data 1"

database "Received Data Map" as ReceivedDataMap {
}

() "Map Add" as MapAdd
() "Map Remove" as MapRemove
ReceivedDataMap - MapAdd
MapRemove -d- ReceivedDataMap

ParseData --> MapAdd : "2. Data 1"


```
