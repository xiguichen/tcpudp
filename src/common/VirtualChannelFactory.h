#pragma once
#include "VirtualChannel.h"

class VirtualChannelFactory {

public:
    VirtualChannelFactory() = default;
    virtual ~VirtualChannelFactory() = default;

    // Method to create a new virtual channel
    virtual VirtualChannel* createChannel() = 0;

    // Method to destroy a virtual channel
    virtual void destroyChannel(VirtualChannel* channel) = 0;

    // Method to get the type of the factory
    virtual const char* getType() const = 0;

};
