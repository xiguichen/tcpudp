#pragma once

class VirtualChannel {

public:
    VirtualChannel() = default;
    virtual ~VirtualChannel() = default;

    // Method to send data through the channel
    virtual void sendData(const char* data, size_t size) = 0;

    // Method to receive data from the channel
    virtual size_t receiveData(char* buffer, size_t bufferSize) = 0;

    // Method to check if the channel is open
    virtual bool isOpen() const = 0;

    // Method to close the channel
    virtual void close() = 0;
};
