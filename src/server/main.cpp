#include "SocketManager.h"
#include <Log.h>
using namespace Logger;

int main(int argc, char* argv[]) {

    Log::getInstance().setLogLevel(LogLevel::LOG_ERROR);
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
