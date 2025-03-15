#include "SocketManager.h"
#include <Log.h>
using namespace Logger;

int main(int argc, char* argv[]) {

    Log::getInstance().setLogLevel(LogLevel::LOG_ERROR);
    SocketManager socketManager;
    socketManager.createSocket();
    socketManager.bindToPort(6001);
    socketManager.listenForConnections();
    while (true) {
        socketManager.acceptConnection();
    }

}
