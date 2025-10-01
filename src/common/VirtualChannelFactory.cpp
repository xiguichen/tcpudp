#include "VirtualChannelFactory.h"
#include "TcpVirtualChannel.h"

VirtualChannelSp VirtualChannelFactory::create(std::vector<SocketFd> fds)
{
    return std::make_shared<TcpVirtualChannel>(fds);
}
