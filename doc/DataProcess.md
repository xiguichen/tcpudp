```plantuml
class ReadableDataProcessThread extends StopableThread {
    +ReadableDataProcessThread()
    ..protected..
     #read() std::shared_ptr<std::vector<char>> 
    #run()
     #processData(data : std::vector<char>)
}

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


class TcpReadableDataProcessThread extends ReadableDataProcessThread
{
    ..protected..
    +read() std::shared_ptr<std::vector<char>> 

    ..private..
    -_socketFd: SocketFd
}


class IDataProcessor {
    +IDataProcessor()
    +processData(data : std::vector<char>)
}


```
