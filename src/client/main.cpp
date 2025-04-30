#include "Client.h"
#include <Log.h>
#ifdef _WIN32
#include <winsock2.h>
#endif
#include <chrono>
#include <thread>

using namespace Logger;

int main() {
    Log::getInstance().setLogLevel(LogLevel::LOG_ERROR);

#ifdef _WIN32
    WSADATA wsaData;
    int wsaStartupResult = WSAStartup(MAKEWORD(2, 2), &wsaData);
    if (wsaStartupResult != 0) {
        Log::getInstance().error("WSAStartup failed with error: " + std::to_string(wsaStartupResult));
        return 1;
    }
#endif

    while(true)
    {
        Log::getInstance().info("Starting Client");
        Client client("config.json");
        client.configure();
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }
    #ifdef _WIN32
        WSACleanup();
    #endif
    
    return 0;
}
