#include "TcpConnectionFactory.h"

TcpConnectionSp TcpConnectionFactory::create(SocketFd fd)
{
    return std::make_shared<TcpConnection>(fd);
}
