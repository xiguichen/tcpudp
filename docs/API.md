# API Reference

## Core Classes

### Client

The `Client` class manages the TCP/UDP connection and virtual channel setup.

#### Constructors

```cpp
Client();  // Default constructor
```

#### Public Methods

##### Connection Management

```cpp
/**
 * Connect to the server with TCP and prepare virtual channel
 * @param address Server IP address or hostname
 * @param port TCP port for initial connection
 * @return true if connection successful, false otherwise
 */
bool connect(const std::string& address, int port);

/**
 * Disconnect from server and clean up resources
 */
void disconnect();

/**
 * Check if client is connected to server
 * @return true if connected, false otherwise
 */
bool isConnected() const;

/**
 * Get the client ID assigned by the server
 * @return Client ID, or -1 if not connected
 */
int getClientId() const;
```

##### UDP Operations

```cpp
/**
 * Send UDP data through virtual channel
 * @param data Pointer to data buffer
 * @param length Length of data in bytes
 * @return true if data queued successfully, false on error
 */
bool sendUDPData(const void* data, size_t length);

/**
 * Send UDP data as string
 * @param data String data to send
 * @return true if data queued successfully, false on error
 */
bool sendUDPData(const std::string& data);

/**
 * Send UDP data with callback for confirmation
 * @param data Pointer to data buffer
 * @param length Length of data in bytes
 * @param callback Function to call when data is confirmed sent
 * @return true if data queued successfully, false on error
 */
bool sendUDPData(const void* data, size_t length, SendCallback callback);
```

##### Configuration

```cpp
/**
 * Set client configuration
 * @param config Client configuration object
 */
void setConfiguration(const ClientConfiguration& config);

/**
 * Get current configuration
 * @return Reference to current configuration
 */
const ClientConfiguration& getConfiguration() const;
```

##### State Management

```cpp
/**
 * Get statistics about the connection
 * @return ClientStatistics object with connection metrics
 */
ClientStatistics getStatistics() const;

/**
 * Reset internal statistics
 */
void resetStatistics();
```

### Server

The `Server` class handles incoming connections and manages multiple clients.

#### Constructors

```cpp
Server();  // Default constructor
```

#### Public Methods

##### Lifecycle Management

```cpp
/**
 * Start the server on configured port
 * @return true if server started successfully, false otherwise
 */
bool start();

/**
 * Stop the server and disconnect all clients
 */
void stop();

/**
 * Check if server is running
 * @return true if server is running, false otherwise
 */
bool isRunning() const;

/**
 * Get the TCP port the server is listening on
 * @return TCP port number
 */
int getPort() const;
```

##### Connection Management

```cpp
/**
 * Get number of active client connections
 * @return Number of connected clients
 */
int getActiveConnectionCount() const;

/**
 * Check if a specific client is connected
 * @param clientId Client ID to check
 * @return true if client is connected, false otherwise
 */
bool isClientConnected(int clientId) const;

/**
 * Disconnect a specific client
 * @param clientId Client ID to disconnect
 */
void disconnectClient(int clientId);

/**
 * Disconnect all clients and close server
 */
void shutdown();
```

##### Broadcasting

```cpp
/**
 * Send data to all connected clients
 * @param data Pointer to data buffer
 * @param length Length of data in bytes
 * @return Number of clients data was sent to
 */
int broadcast(const void* data, size_t length);

/**
 * Send data to all connected clients
 * @param data String data to send
 * @return Number of clients data was sent to
 */
int broadcast(const std::string& data);

/**
 * Send data to specific client
 * @param clientId Target client ID
 * @param data Pointer to data buffer
 * @param length Length of data in bytes
 * @return true if data sent successfully, false if client not connected
 */
bool sendToClient(int clientId, const void* data, size_t length);

/**
 * Send data to specific client
 * @param clientId Target client ID
 * @param data String data to send
 * @return true if data sent successfully, false if client not connected
 */
bool sendToClient(int clientId, const std::string& data);
```

##### Configuration

```cpp
/**
 * Set server configuration
 * @param config Server configuration object
 */
void setConfiguration(const ServerConfiguration& config);

/**
 * Get current configuration
 * @return Reference to current configuration
 */
const ServerConfiguration& getConfiguration() const;
```

### VirtualChannel

The virtual channel manages multiple TCP connections for reliable message delivery.

#### Public Methods

##### Message Handling

```cpp
/**
 * Send a message through the virtual channel
 * @param message Message to send
 * @return true if message queued successfully, false on error
 */
bool send(const Message& message);

/**
 * Receive a message from the virtual channel
 * @param timeoutMs Maximum time to wait for message (milliseconds)
 * @return Message object, or empty if timeout or error
 */
Message receive(int timeoutMs = 1000);

/**
 * Try to receive a message without blocking
 * @param message Reference to store received message
 * @return true if message received, false if no message available
 */
bool tryReceive(Message& message);
```

##### Callbacks

```cpp
/**
 * Set callback for received messages
 * @param callback Function to call when message is received
 */
void setReceiveCallback(ReceiveCallback callback);

/**
 * Set callback for disconnection events
 * @param callback Function to call when connection is lost
 */
void setDisconnectCallback(DisconnectCallback callback);

/**
 * Set callback for connection events
 * @param callback Function to call when connection is established
 */
void setConnectionCallback(ConnectionCallback callback);
```

##### Status

```cpp
/**
 * Check if virtual channel is open
 * @return true if channel is open, false otherwise
 */
bool isOpen() const;

/**
 * Get number of active TCP connections
 * @return Number of active connections
 */
int getConnectionCount() const;

/**
 * Get statistics for the virtual channel
 * @return ChannelStatistics object with metrics
 */
ChannelStatistics getStatistics() const;
```

### TcpConnection

Manages individual TCP connections within a virtual channel.

#### Public Methods

```cpp
/**
 * Check if connection is connected
 * @return true if connected, false otherwise
 */
bool isConnected() const;

/**
 * Get connection statistics
 * @return ConnectionStatistics with metrics
 */
ConnectionStatistics getStatistics() const;

/**
 * Set connection timeout
 * @param timeoutMs Timeout in milliseconds
 */
void setTimeout(int timeoutMs);

/**
 * Get connection file descriptor
 * @return Socket file descriptor
 */
Socket getSocket() const;
```

### Socket Utilities

Cross-platform socket operations and utilities.

#### Core Functions

```cpp
/**
 * Create a TCP socket
 * @return Socket handle, or invalid socket on error
 */
Socket createTCPSocket();

/**
 * Create a UDP socket
 * @return Socket handle, or invalid socket on error
 */
Socket createUDPSocket();

/**
 * Bind a socket to address and port
 * @param socket Socket to bind
 * @param address IP address
 * @param port Port number
 * @return true if successful, false otherwise
 */
bool bindSocket(Socket socket, const std::string& address, int port);

/**
 * Connect a TCP socket to a server
 * @param socket Socket to connect
 * @param address Server address
 * @param port Server port
 * @return true if successful, false otherwise
 */
bool connectSocket(Socket socket, const std::string& address, int port);
```

#### Send/Receive Operations

```cpp
/**
 * Send data on a socket
 * @param socket Socket to send on
 * @param data Pointer to data buffer
 * @param length Length of data
 * @param result Reference to store result
 * @return true if operation initiated, false on error
 */
bool send(Socket socket, const void* data, size_t length, SocketResult& result);

/**
 * Receive data from a socket
 * @param socket Socket to receive from
 * @param buffer Buffer to store received data
 * @param maxLength Maximum length to receive
 * @param result Reference to store result
 * @return true if operation initiated, false on error
 */
bool recv(Socket socket, void* buffer, size_t maxLength, SocketResult& result);
```

#### Non-blocking I/O

```cpp
/**
 * Check if socket is readable
 * @param socket Socket to check
 * @param timeoutMs Timeout in milliseconds
 * @return true if socket has data to read, false otherwise
 */
bool IsSocketReadable(Socket socket, int timeoutMs = 100);

/**
 * Check if socket is writable
 * @param socket Socket to check
 * @param timeoutMs Timeout in milliseconds
 * @return true if socket can be written to, false otherwise
 */
bool IsSocketWritable(Socket socket, int timeoutMs = 100);

/**
 * Set socket to non-blocking mode
 * @param socket Socket to modify
 * @return true if successful, false otherwise
 */
bool setNonBlocking(Socket socket);

/**
 * Set socket timeout
 * @param socket Socket to modify
 * @param timeoutMs Timeout in milliseconds
 * @return true if successful, false otherwise
 */
bool setTimeout(Socket socket, int timeoutMs);
```

## Protocol Structures

### VC Protocol

```cpp
// Virtual Channel Header
struct VCHeader {
    uint8_t type;      // Packet type (0 = Data)
    uint16_t msgId;    // Message identifier
};

// Virtual Channel Data Packet
#pragma pack(push, 1)
struct VCDataPacket {
    VCHeader header;
    uint16_t length;   // Data length (max 2000)
    uint8_t data[VC_MAX_DATA_PAYLOAD_SIZE];
};
#pragma pack(pop);
```

### UVT Protocol

```cpp
// UDP over TCP Header
#pragma pack(push, 1)
struct UvtHeader {
    uint16_t size;      // Total packet size
    uint16_t msgId;     // Message identifier
    uint16_t checksum;  // XOR checksum of payload
};
#pragma pack(pop);

// Bind message
#pragma pack(push, 1)
struct MsgBind {
    uint8_t type;      // Message type
    uint16_t clientId; // Client ID
    uint16_t padding;
};
#pragma pack(pop);

// Bind response message
#pragma pack(push, 1)
struct MsgBindResponse {
    uint8_t type;      // Message type
    uint16_t status;   // 0 = Success
    uint16_t padding;
};
#pragma pack(pop);
```

## Configuration Structures

### ClientConfiguration

```cpp
struct ClientConfiguration {
    int localHostUdpPort = 5002;    // Local UDP port
    int peerTcpPort = 7001;         // Server TCP port
    std::string peerAddress = "localhost";  // Server address
    int clientId = 1;                // Client ID (overridden by server)
    int connectTimeout = 5000;      // Connection timeout (ms)
    int reconnectDelay = 1000;      // Reconnect delay (ms)
    int maxReconnectAttempts = 5;   // Maximum reconnect attempts
    int recvBufferSize = 65536;     // TCP receive buffer size
    int sendBufferSize = 65536;     // TCP send buffer size
    int virtualChannelConnections = 8; // Number of TCP connections for VC
    int sendQueueThreshold = 500;    // Drop threshold for send queues
};
```

### ServerConfiguration

```cpp
struct ServerConfiguration {
    int tcpPort = 7001;              // TCP listening port
    int maxConnections = 100;        // Maximum concurrent connections
    int backlog = 50;                // Listen backlog size
    int recvBufferSize = 65536;     // TCP receive buffer size
    int sendBufferSize = 65536;     // TCP send buffer size
    int idleTimeout = 30000;        // Idle connection timeout (ms)
    int threadPoolSize = 4;         // Worker thread pool size
};
```

## Error Codes

### Socket Error Codes

```cpp
// Socket operation results
enum SocketError {
    SOCKET_ERROR_NONE = 0,           // Success
    SOCKET_ERROR_WOULD_BLOCK,        // Non-blocking operation would block
    SOCKET_ERROR_TIMEOUT,            // Operation timed out
    SOCKET_ERROR_CONNECTION_RESET,   // Connection reset by peer
    SOCKET_ERROR_CONNECTION_REFUSED, // Connection refused
    SOCKET_ERROR_NOT_CONNECTED,      // Socket not connected
    SOCKET_ERROR_ADDR_IN_USE,       // Address already in use
    SOCKET_ERROR_NO_MEM,            // Out of memory
    SOCKET_ERROR_INVALID,           // Invalid socket
    SOCKET_ERROR_UNKNOWN            // Unknown error
};
```

### Protocol Error Codes

```cpp
// Protocol error codes
enum ProtocolError {
    PROTO_ERROR_NONE = 0,           // Success
    PROTO_ERROR_INVALID_LENGTH,     // Invalid packet length
    PROTO_ERROR_CHECKSUM_MISMATCH, // Checksum failed
    PROTO_ERROR_UNKNOWN_PACKET,    // Unknown packet type
    PROTO_ERROR_QUEUE_FULL,        // Send/receive queue full
    PROTO_ERROR_CONNECTION_LOST     // Connection lost
};
```

## Callback Types

```cpp
// Message received callback
using ReceiveCallback = std::function<void(const Message& message)>;

// Connection callback
using ConnectionCallback = std::function<void(int connectionId)>;

// Disconnect callback
using DisconnectCallback = std::function<void(int connectionId)>;

// Send confirmation callback
using SendCallback = std::function<void(bool success, const Message& message)>;

// Error callback
using ErrorCallback = std::function<void(int errorCode, const std::string& message)>;
```

## Statistics Structures

### ClientStatistics

```cpp
struct ClientStatistics {
    int64_t bytesSent = 0;          // Total bytes sent
    int64_t bytesReceived = 0;     // Total bytes received
    int32_t packetsSent = 0;        // Total packets sent
    int32_t packetsReceived = 0;    // Total packets received
    int32_t connectionAttempts = 0;  // Connection attempts
    int32_t reconnections = 0;       // Reconnection attempts
    double avgLatency = 0.0;        // Average latency (ms)
    double minLatency = 0.0;        // Minimum latency (ms)
    double maxLatency = 0.0;        // Maximum latency (ms)
};
```

### ChannelStatistics

```cpp
struct ChannelStatistics {
    int activeConnections = 0;       // Number of active TCP connections
    int64_t totalBytesSent = 0;     // Total bytes sent through VC
    int64_t totalBytesReceived = 0; // Total bytes received through VC
    int32_t messagesSent = 0;       // Messages sent
    int32_t messagesReceived = 0;   // Messages received
    double avgMessageSize = 0.0;    // Average message size
    int32_t queueSize = 0;          // Current queue size
    int32_t droppedPackets = 0;    // Packets dropped due to queue overflow
};
```

## Utility Functions

### Performance Counting

```cpp
// Global performance counter
extern PerformanceCounter g_perfCounter;

// Track function performance
#define PERF_SCOPE(name) PerformanceScope perfScope(name)

// Example usage
{
    PERF_SCOPE("sendUDPData");
    // Function code here
}
```

### Logging

```cpp
// Logging macros
log_debug("Debug message: %d", value);
log_info("Info message: %s", message);
log_warning("Warning message");
log_error("Error message: %d", errorCode);

// Set log level (command line: --log-level=DEBUG|INFO|WARNING|ERROR)
void setLogLevel(LogLevel level);
```

### Threading Utilities

```cpp
// Thread-safe queue
template<typename T>
class BlockingQueue {
public:
    void push(const T& item);
    T pop(int timeoutMs = -1);
    bool empty() const;
    size_t size() const;
};

// Stopable thread base class
class StopableThread {
public:
    void start();
    void stop();
    bool isRunning() const;
    void join();
protected:
    virtual void run() = 0;
};
```

## Best Practices

1. **Error Handling**: Always check return values from socket operations
2. **Resource Management**: Use RAII for socket handles and connections
3. **Thread Safety**: Use mutexes for shared state between threads
4. **Configuration**: Load configurations from files before starting
5. **Logging**: Use appropriate log levels for different scenarios
6. **Performance**: Use performance counters to identify bottlenecks
7. **Memory**: Monitor queue sizes to prevent memory leaks