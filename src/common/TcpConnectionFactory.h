#pragma once
#include "TcpConnection.h"
#include "Socket.h"

class TcpConnectionFactory
{
  public:
    TcpConnectionFactory() = default;
    ~TcpConnectionFactory() = default;

    // Method to create a new TCP connection
    static TcpConnectionSp create(SocketFd fd);
};
