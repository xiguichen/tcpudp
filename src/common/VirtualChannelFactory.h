#pragma once
#include <vector>
#include "VirtualChannel.h"
#include "Socket.h"

class VirtualChannelFactory
{

  public:
    VirtualChannelFactory() = default;
    ~VirtualChannelFactory() = default;

    // Method to create a new virtual channel
    static VirtualChannelSp create(std::vector<SocketFd> fds);
};
