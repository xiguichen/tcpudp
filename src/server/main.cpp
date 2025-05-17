#include "SocketManager.h"
#include <Log.h>
#include "../common/PerformanceMonitor.h"
using namespace Logger;

#ifdef EXCLUDE_MAIN

#else

int main(int argc, char* argv[]) {

    Log::getInstance().setLogLevel(LogLevel::LOG_INFO);
    
    // Start performance monitoring
    PerformanceMonitor::getInstance().startMonitoring(30); // Report every 30 seconds
    Log::getInstance().info("Performance monitoring started");
    
    SocketManager socketManager;
    socketManager.createSocket();
    socketManager.bindToPort(6001);
    socketManager.listenForConnections();
    socketManager.startTcpQueueToUdpThreadPool();
    socketManager.startUdpQueueToTcpThreadPool();
    
    Log::getInstance().info("Server started successfully with lock-free queues");
    
    while (true) {
        socketManager.acceptConnection();
    }

}
#endif
