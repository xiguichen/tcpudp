#include "udpserver.h"
#include "config.h"
#include <iostream>
using namespace std;

int main()
{
    UdpServerConfig config = {
        .localAddress = "127.0.0.1",
        .localPort = 8080,
        .remoteAddress = "127.0.0.1",
        .remotePort = 8081,
    };
    UdpServer server(config);;
    server.start();
    server.stop();
}
