#pragma once


#include "VirtualChannel.h"

class MultipleTcpConnVirtualChannel : public VirtualChannel
{

public:
    MultipleTcpConnVirtualChannel() = default;
    virtual ~MultipleTcpConnVirtualChannel() = default;

    // Method to send data through the channel
    virtual void sendData(const char* data, size_t size) override;

    // Method to receive data from the channel
    virtual size_t receiveData(char* buffer, size_t bufferSize) override;

    // Method to check if the channel is open
    virtual bool isOpen() const override;

    // Method to close the channel
    virtual void close() override;
};

