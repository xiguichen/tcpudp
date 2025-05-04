#include "SocketManager.h"
#include <Log.h>
using namespace Logger;

#ifdef EXCLUDE_MAIN

#else

int main(int argc, char* argv[]) {

    Log::getInstance().setLogLevel(LogLevel::LOG_INFO);
    SocketManager socketManager;
    socketManager.createSocket();
    socketManager.bindToPort(6001);
    socketManager.listenForConnections();
    socketManager.startTcpQueueToUdpThreadPool();
    socketManager.startUdpQueueToTcpThreadPool();
    while (true) {
        socketManager.acceptConnection();
    }

}
#endif
