# Non-Blocking I/O Implementation

This document describes the implementation of non-blocking I/O in the TCP/UDP server and client applications.

## Overview

The codebase has been updated to use non-blocking I/O operations for all socket communications. This approach offers several advantages:

1. **Improved Scalability**: Non-blocking I/O allows a single thread to handle multiple connections efficiently.
2. **Reduced Resource Usage**: Fewer threads are needed to handle the same number of connections.
3. **Better Responsiveness**: The application can continue processing other tasks while waiting for I/O operations.
4. **Timeout Control**: All operations now have configurable timeouts to prevent indefinite blocking.

## Implementation Details

### 1. Socket Abstraction Layer

A comprehensive socket abstraction layer has been implemented in `Socket.h` and `Socket.cpp` that provides:

- Cross-platform socket operations (Windows, Linux, macOS)
- Non-blocking socket configuration
- Timeout-based I/O operations
- Error handling with specific error codes

### 2. Key Components

#### Socket Creation and Configuration

```cpp
// Create a socket with error handling
SocketFd CreateSocket(int domain, int type, int protocol);

// Set socket to non-blocking mode
int SetSocketNonBlocking(SocketFd socketFd);

// Set socket to blocking mode
int SetSocketBlocking(SocketFd socketFd);

// Check if socket is in non-blocking mode
bool IsSocketNonBlocking(SocketFd socketFd);
```

#### Non-blocking I/O Operations

```cpp
// Non-blocking send with timeout
ssize_t SendTcpDataNonBlocking(SocketFd socketFd, const void *data, size_t length, int flags, int timeoutMs);

// Non-blocking receive with timeout
ssize_t RecvTcpDataNonBlocking(SocketFd socketFd, void *buffer, size_t bufferSize, int flags, int timeoutMs);

// Non-blocking UDP send with timeout
ssize_t SendUdpDataNonBlocking(SocketFd socketFd, const void *data, size_t length, int flags,
                              const struct sockaddr *destAddr, socklen_t destAddrLen, int timeoutMs);

// Non-blocking UDP receive with timeout
ssize_t RecvUdpDataNonBlocking(SocketFd socketFd, void *buffer, size_t bufferSize, int flags,
                              struct sockaddr *srcAddr, socklen_t *srcAddrLen, int timeoutMs);
```

#### Socket Polling

```cpp
// Check if socket is readable with timeout
bool IsSocketReadable(SocketFd socketFd, int timeoutMs);

// Check if socket is writable with timeout
bool IsSocketWritable(SocketFd socketFd, int timeoutMs);

// Poll socket for specific events with timeout
int SocketPoll(SocketFd socketFd, int events, int timeoutMs);
```

### 3. Error Handling

A set of error codes has been defined to handle non-blocking I/O specific errors:

```cpp
enum SocketError {
    SOCKET_ERROR_NONE = 0,
    SOCKET_ERROR_WOULD_BLOCK = -1,
    SOCKET_ERROR_TIMEOUT = -2,
    SOCKET_ERROR_CLOSED = -3,
    SOCKET_ERROR_INTERRUPTED = -4,
    SOCKET_ERROR_OTHER = -5
};
```

These error codes allow the application to distinguish between different types of errors and handle them appropriately.

### 4. Cross-Platform Support

The implementation supports multiple platforms:

- **Windows**: Uses Winsock2 API with `ioctlsocket` for non-blocking mode
- **Linux/macOS**: Uses POSIX API with `fcntl` for non-blocking mode
- **Polling**: Uses `select` on Windows and `poll` on POSIX systems

## Usage Examples

### Server-side Non-blocking Accept

```cpp
void SocketManager::acceptConnection() {
    // Check if server socket is readable with a short timeout
    if (!IsSocketReadable(serverSocket, 100)) { // 100ms timeout
        // No pending connections, yield to other threads
        std::this_thread::yield();
        return;
    }
    
    // Accept connection non-blocking
    int clientSocket = accept(serverSocket, ...);
    
    // Handle potential errors
    if (clientSocket < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            // No pending connections, just return
            return;
        }
        // Handle other errors
    }
    
    // Set client socket to non-blocking mode
    SetSocketNonBlocking(clientSocket);
    
    // Continue with connection handling
}
```

### Client-side Non-blocking Receive

```cpp
std::vector<char> PeerTcpSocket::receive() {
    // Check if socket is readable with timeout
    if (!IsSocketReadable(socketFd, 1000)) { // 1 second timeout
        return {}; // No data available
    }

    // Receive data with timeout
    ssize_t bytesReceived = RecvTcpDataNonBlocking(socketFd, buffer, bufferSize, 0, 5000);
    
    if (bytesReceived > 0) {
        // Process received data
    } else if (bytesReceived == SOCKET_ERROR_TIMEOUT) {
        // Handle timeout
    } else if (bytesReceived == SOCKET_ERROR_WOULD_BLOCK) {
        // Would block, try again later
    } else if (bytesReceived == SOCKET_ERROR_CLOSED) {
        // Connection closed
    }
}
```

## Performance Considerations

1. **Timeout Values**: Timeout values have been carefully chosen based on the expected response times:
   - Short timeouts (100ms) for polling operations
   - Medium timeouts (1s) for regular I/O operations
   - Longer timeouts (5s) for connection establishment and critical operations

2. **Thread Yielding**: When no data is available, threads yield control to allow other threads to run:
   ```cpp
   std::this_thread::yield();
   ```

3. **Error Handling**: Comprehensive error handling ensures the application can recover from transient errors.

## Conclusion

The non-blocking I/O implementation significantly improves the performance and scalability of the TCP/UDP server and client applications. By eliminating blocking operations, the application can handle more connections with fewer resources and provide better responsiveness under high load.