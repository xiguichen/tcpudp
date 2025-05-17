#ifndef ISOCKET_H
#define ISOCKET_H

#include <vector>
#include <string>

// Interface for socket operations
class ISocket {
public:
    virtual ~ISocket() = default;
    
    // Connect to a remote host
    virtual void connect(const std::string& address, int port) = 0;
    
    // Send data
    virtual void send(const std::vector<char>& data) = 0;
    
    // Receive data
    virtual std::vector<char> receive() = 0;
    
    // Close the socket
    virtual void close() = 0;
};

#endif // ISOCKET_H