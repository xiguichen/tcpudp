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

queue "Data Queue" as DataQueue {
}
() "Data Dequeue" as DataDequeue
DataQueue - DataDequeue

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
DataWriteThread --> DataDequeue : "1. Data 1\n 6. Data 2"
DataWriteThread -u-> SocketWrite: "2. Data 1\n 7. Data 2"
DataWriteThread -l-> MapAdd : "3. Data 1, wait for Ack 1"
DataReadThread -u-> SocketRead : "4. Ack 1"
DataReadThread -d-> DataAckCallback : "5. Ack 1"
DataAckCallback -> MapRemove : "6. Data 1"

```

### Receive data from remote

```plantuml

component "Socket 1" as Socket1 {
}

() "Socket Read" as SocketRead
SocketRead -u- Socket1


component "Socket 2" as Socket2 {
}
() "Socket Write" as SocketWrite
Socket2 -- SocketWrite


component "DataReadThread" as DataReadThread {
}

queue "Ack Queue" as AckQueue {
}
() "Dequeue" as AckDequeue
() "Enqueue" as AckEnqueue

AckQueue - AckDequeue
AckQueue -- AckEnqueue

() "Parse Data" as ParseData
DataReadThread -- ParseData

component "AckWriteThread" as AckWriteThread {
}

DataReadThread -u-> SocketRead : "1. Data 1"

database "Received Data Map" as ReceivedDataMap {
}

() "Map Add" as MapAdd
ReceivedDataMap - MapAdd

ParseData --> MapAdd : "2. Data 1"

MapAdd -> AckEnqueue : "3. Ack 1"
AckWriteThread --> AckDequeue : "4. Ack 1"
AckWriteThread -u-> SocketWrite : "5. Ack 1"


```


### Packet Reordering

```plantuml
database "Received Data Map" as ReceivedDataMap {
}
() "Remove" as MapRemove
() "Add" as MapAdd
ReceivedDataMap -l- MapRemove
ReceivedDataMap  -r- MapAdd

component "DataPorter" as DataPorter {
}

() "New Data Callback" as NewDataCallback
DataPorter - NewDataCallback

database "Reordered Data Queue" as ReorderedDataQueue {
}
() "Reorder Enqueue" as ReorderEnqueue
ReorderedDataQueue -u- ReorderEnqueue

component "PackerCounter" as PackerCounter {
}
() "Next Frame ID" as NextFrameId
PackerCounter -d- NextFrameId


MapAdd --> NewDataCallback : "1. Notify Data 1"
DataPorter -d-> NextFrameId : "2. frame ID"
DataPorter -> MapRemove : "3. Data 1 if frameId is next"
DataPorter --> ReorderEnqueue : "4. Data 1"

```
```
