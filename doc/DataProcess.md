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
