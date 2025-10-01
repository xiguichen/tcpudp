#include "VirtualChannel.h"

void VirtualChannel::setReceiveCallback(std::function<void(const char *data, size_t size)> callback)
{
    receiveCallback = callback;
}

