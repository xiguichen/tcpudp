#pragma once

#include <cstddef>
#include <functional>
#include <memory>
class VirtualChannel
{

  public:
    VirtualChannel() = default;
    virtual ~VirtualChannel() = default;

    // Method to open the channel
    virtual void open() = 0;

    // Method to send data through the channel
    virtual void send(const char *data, size_t size) = 0;

    // Method to check if the channel is open
    virtual bool isOpen() const = 0;

    // Method to close the channel
    virtual void close() = 0;

    // Set the receive callback
    void setReceiveCallback(std::function<void(const char *data, size_t size)> callback);

    // Set the disconnect callback (VC-wide disconnect notification)
    void setDisconnectCallback(std::function<void()> callback) { disconnectCallback = std::move(callback); }

    // Set the teardown callback. Unlike disconnectCallback, this runs exactly
    // once when close() finishes, including an explicit close or shutdown.
    // It is the place for owner-side cleanup that must happen for every close
    // path (removing the channel from a registry, closing its UDP socket, ...).
    void setTeardownCallback(std::function<void()> callback) { teardownCallback = std::move(callback); }

  protected:
    // OnReceiveCallback
    std::function<void(const char *data, size_t size)> receiveCallback = nullptr;
    // OnDisconnectCallback
    std::function<void()> disconnectCallback = nullptr;
    // OnTeardownCallback
    std::function<void()> teardownCallback = nullptr;
};

typedef std::shared_ptr<VirtualChannel> VirtualChannelSp;
