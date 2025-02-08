#pragma once
#include <netinet/in.h>
#include <string>


typedef struct _UdpServerConfig
{
    std::string localAddress;
    unsigned short localPort;
    std::string remoteAddress;
    unsigned short remotePort;

} UdpServerConfig;
