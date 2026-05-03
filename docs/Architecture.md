# Architecture Documentation

## System Architecture

### High-Level Overview

The TCP/UDP project implements a sophisticated networking system that provides reliable UDP communication over multiple TCP connections. This architecture enables UDP-like semantics while leveraging TCP's reliability features.

```
┌─────────────────────────────────────────────────────────────┐
│                     Application Layer                       │
│  ┌─────────────┐  ┌─────────────┐  ┌─────────────┐        │
│  │   Client    │  │   Server    │  │   Test     │        │
│  │   App       │  │   App       │  │   Apps     │        │
│  └─────────────┘  └─────────────┘  └─────────────┘        │
└─────────────────────────────────────────────────────────────┘
                              │
┌─────────────────────────────────────────────────────────────┐
│                  Virtual Channel Layer                      │
│  ┌─────────────────────────────────────────────────────────┐ │
│  │          TcpVirtualChannel                              │ │
│  │  ┌─────────┐ ┌─────────┐ ┌─────────┐ ┌─────────┐      │ │
│  │  │ VC Conn1 │ │ VC Conn2 │ │ VC Conn3 │ │ ...    │      │ │
│  │  └─────────┘ └─────────┘ └─────────┘ └─────────┘      │ │
│  └─────────────────────────────────────────────────────────┘ │
└─────────────────────────────────────────────────────────────┘
                              │
┌─────────────────────────────────────────────────────────────┐
│                   Protocol Layer                            │
│  ┌─────────────────────────────────────────────────────────┐ │
│  │                Protocol                                │ │
│  │  ┌─────────────┐ ┌─────────────┐ ┌─────────────┐      │ │
│  │  │   VC Prot  │ │   UVT Prot │ │   Bind     │      │ │
│  │  └─────────────┘ └─────────────┘ └─────────────┘      │ │
│  └─────────────────────────────────────────────────────────┘ │
└─────────────────────────────────────────────────────────────┘
                              │
┌─────────────────────────────────────────────────────────────┐
│                  Socket Layer                              │
│  ┌─────────────────────────────────────────────────────────┐ │
│  │              Socket Abstraction                         │ │
│  │  ┌─────────┐ ┌─────────┐ ┌─────────┐ ┌─────────┐      │ │
│  │  │ TCP Sock │ │ UDP Sock │ │ Polling │ │ Timeout │      │ │
│  │  └─────────┘ └─────────┘ └─────────┘ └─────────┘      │ │
│  └─────────────────────────────────────────────────────────┘ │
└─────────────────────────────────────────────────────────────┘
                              │
┌─────────────────────────────────────────────────────────────┐
│                   Operating System                         │
│  ┌─────────────────────────────────────────────────────────┐ │
│  │             Platform-Specific                          │ │
│  │  ┌─────────┐ ┌─────────┐ ┌─────────┐ ┌─────────┐      │ │
│  │  │ Windows │ │  Linux   │ │ macOS   │ │ BSD     │      │ │
│  │  │  Sockets│ │  Sockets │ │ Sockets │ │ Sockets │      │ │
│  │  └─────────┘ └─────────┘ └─────────┘ └─────────┘      │ │
│  └─────────────────────────────────────────────────────────┘ │
└─────────────────────────────────────────────────────────────┘
```

## Layer-by-Layer Architecture

### 1. Application Layer

The application layer consists of three main components:

- **Client App**: User-facing application that sends and receives UDP data
- **Server App**: Handles incoming connections and manages client data
- **Test Apps**: Python and C++ test applications for validation

#### Responsibilities:
- User interface/business logic
- Data serialization/deserialization
- Configuration management
- Error handling and logging

### 2. Virtual Channel Layer

The virtual channel layer abstracts multiple TCP connections into a single reliable message channel.

#### Key Components:

**TcpVirtualChannel**
```cpp
class TcpVirtualChannel : public VirtualChannel {
private:
    std::vector<TcpConnectionSp> connections;  // 8 TCP connections
    std::atomic<bool> isOpen;
    std::mutex vcsMutex;
    
public:
    // Creates virtual channel from multiple file descriptors
    static TcpVirtualChannelSp create(const std::vector<Socket>& fds);
    
    // Message sending (distributed across connections)
    bool send(const Message& message) override;
    
    // Message receiving (from any connection)
    bool receive(Message& message, int timeoutMs) override;
};
```

**Connection Management**
- Uses 8 concurrent TCP connections for parallel processing
- Implements round-robin distribution for outgoing messages
- Aggregates incoming messages from all connections
- Maintains connection state and health monitoring

**Queue Management**
```cpp
// Per-connection send queues
struct ConnectionQueue {
    BlockingQueue<Message> sendQueue;
    BlockingQueue<Message> recvQueue;
    size_t maxSize = SEND_QUEUE_DROP_THRESHOLD;
};
```

### 3. Protocol Layer

The protocol layer defines binary formats for data transmission and includes two main protocols:

#### VC Protocol (Virtual Channel)
- **Purpose**: Reliable message delivery over multiple TCP connections
- **Format**: Binary packets with sequence IDs and checksums
- **Features**: Message ordering, fragmentation handling, error detection

**VCDataPacket Structure**
```cpp
#pragma pack(push, 1)
struct VCHeader {
    uint8_t type;      // Packet type (0 = Data)
    uint16_t msgId;    // Message sequence ID
};

struct VCDataPacket {
    VCHeader header;
    uint16_t length;   // Data length (max 2000 bytes)
    uint8_t data[VC_MAX_DATA_PAYLOAD_SIZE];
};
#pragma pack(pop);
```

#### UVT Protocol (UDP over TCP)
- **Purpose**: Encapsulates UDP packets for transmission over TCP
- **Features**: Size prefixing, XOR checksum, message ID tracking
- **Handshake**: Bind/BindResponse for client identification

**Protocol Flow**
```cpp
// Client → Server: Bind Request
MsgBind {
    type = 1,
    clientId = 1,
    padding = 0
}

// Server → Client: Bind Response
MsgBindResponse {
    type = 2,
    status = 0,  // Success
    padding = 0
}
```

### 4. Socket Layer

The socket layer provides cross-platform socket operations with non-blocking I/O.

#### Socket Abstraction
```cpp
// Platform-independent socket type
#ifdef _WIN32
using Socket = SOCKET;
#else
using Socket = int;
#endif

// Cross-platform socket operations
class SocketManager {
public:
    Socket createSocket(SocketType type);
    bool close(Socket socket);
    bool setNonBlocking(Socket socket, bool enable);
    bool setTimeout(Socket socket, int timeoutMs);
    bool setBufferSize(Socket socket, int recvSize, int sendSize);
};
```

#### Non-blocking I/O
- **Polling**: Uses `select()`/`WSAPoll()` for event detection
- **Timeouts**: Configurable timeout for all socket operations
- **Error Handling**: Distinguishes between temporary and permanent errors

**Socket Operations**
```cpp
// Check socket readability with timeout
bool IsSocketReadable(Socket socket, int timeoutMs = 100);

// Check socket writability with timeout  
bool IsSocketWritable(Socket socket, int timeoutMs = 100);

// Non-blocking send with timeout
SocketResult send(Socket socket, const void* data, size_t length);

// Non-blocking receive with timeout
SocketResult recv(Socket socket, void* buffer, size_t maxLength);
```

## Threading Model

The system uses a sophisticated threading model for optimal performance:

### Thread Architecture

```
┌─────────────────────────────────────────────────────────────┐
│                    Main Thread                             │
│  (Application logic, configuration, connection management)  │
└─────────────────────────────────────────────────────────────┘
                              │
┌─────────────────────────────────────────────────────────────┐
│                 Virtual Channel Threads                   │
│  ┌─────────────┐ ┌─────────────┐ ┌─────────────┐         │
│  │ Read Thread │ │ Write Thread│ │ ... (8 total)│         │
│  │ per TCP     │ │ per TCP     │ │             │         │
│  │ Connection  │ │ Connection  │ │             │         │
│  └─────────────┘ └─────────────┘ └─────────────┘         │
└─────────────────────────────────────────────────────────────┘
                              │
┌─────────────────────────────────────────────────────────────┐
│                 Data Processing Threads                    │
│  ┌─────────────┐ ┌─────────────┐ ┌─────────────┐         │
│  │ Processor 1 │ │ Processor 2 │ │ Processor 3 │         │
│  │ (TCP → UDP) │ │ (UDP → TCP) │ │ (Monitoring)│         │
│  └─────────────┘ └─────────────┘ └─────────────┘         │
└─────────────────────────────────────────────────────────────┘
```

### Thread Classes

**StopableThread Base Class**
```cpp
class StopableThread {
private:
    std::atomic<bool> running;
    std::thread thread;
    
public:
    virtual ~StopableThread();
    void start();
    void stop();
    void join();
    bool isRunning() const;
    
protected:
    virtual void run() = 0;
};
```

**TcpVCReadThread**
```cpp
class TcpVCReadThread : public StopableThread {
private:
    TcpConnectionSp connection;
    BlockingQueue<Message>& outputQueue;
    
protected:
    void run() override;
    
public:
    TcpVCReadThread(TcpConnectionSp conn, BlockingQueue<Message>& queue);
};
```

**TcpVCWriteThread**
```cpp
class TcpVCWriteThread : public StopableThread {
private:
    TcpConnectionSp connection;
    BlockingQueue<Message>& inputQueue;
    
protected:
    void run() override;
    
public:
    TcpVCWriteThread(TcpConnectionSp conn, BlockingQueue<Message>& queue);
};
```

### Data Flow Between Threads

1. **Application → Virtual Channel**: UDP data enqueued
2. **Virtual Channel → Write Threads**: Messages distributed to TCP connections
3. **Read Threads → Virtual Channel**: Received messages aggregated
4. **Virtual Channel → Application**: Delivered via callback

## Data Flow Architecture

### Client Data Flow

```
Application
    ↓ (sendUDPData)
Virtual Channel (round-robin distribution)
    ↓ (message across 8 TCP connections)
TcpVCWriteThread (per connection)
    ↓ (non-blocking send)
TCP Socket
    ↓ (network)
Server TCP Socket
    ↓ (non-blocking recv)
TcpVCReadThread (per connection)
    ↓ (message aggregation)
Virtual Channel
    ↓ (callback)
Application
```

### Server Data Flow

```
Server Listener Socket
    ↓ (accept)
Peer (TCP socket + UDP address)
    ↓ (data received)
Virtual Channel
    ↓ (distribution to connections)
TcpVCReadThreads
    ↓ (message processing)
Data Processor
    ↓ (UDP extraction)
UDP Socket
    ↓ (network)
Client UDP Socket
```

## Memory Management

### Smart Pointer Usage

```cpp
// Shared pointers for shared ownership
using TcpConnectionSp = std::shared_ptr<TcpConnection>;
using VirtualChannelSp = std::shared_ptr<VirtualChannel>;

// Unique pointers for exclusive ownership
using TcpConnectionUp = std::unique_ptr<TcpConnection>;

// Type aliases for cleaner code
using Message = std::vector<uint8_t>;
using ClientId = int;
using ConnectionId = int;
```

### Queue Management

```cpp
// Thread-safe queues with size limits
template<typename T>
class BlockingQueue {
private:
    std::queue<T> queue;
    std::mutex mutex;
    std::condition_variable cv;
    size_t maxSize;
    
public:
    bool push(const T& item);
    bool pop(T& item, int timeoutMs = -1);
    size_t size() const;
    bool empty() const;
};
```

### Resource Cleanup

```cpp
// RAII socket wrapper
class SocketGuard {
private:
    Socket socket;
    
public:
    SocketGuard(Socket s) : socket(s) {}
    ~SocketGuard() { close(socket); }
    operator Socket() { return socket; }
};
```

## Error Handling Architecture

### Error Code Hierarchy

```
┌─────────────────────────────────────────────────────────────┐
│                       Error Codes                          │
│  ┌─────────────┐ ┌─────────────┐ ┌─────────────┐         │
│  │ Socket      │ │ Protocol    │ │ Application │         │
│  │ Errors      │ │ Errors      │ │ Errors      │         │
│  └─────────────┘ └─────────────┘ └─────────────┘         │
└─────────────────────────────────────────────────────────────┘
```

### Error Handling Patterns

```cpp
// Result pattern for operations
template<typename T>
class Result {
private:
    T value;
    ErrorCode error;
    
public:
    bool isOk() const;
    T& getValue();
    ErrorCode getError() const;
};

// Example usage
Result<Message> result = channel.receive(1000);
if (result.isOk()) {
    Message msg = result.getValue();
} else {
    log_error("Receive failed: %d", result.getError());
}
```

### Recovery Mechanisms

1. **Connection Recovery**: Exponential backoff reconnection
2. **Queue Recovery**: Automatic dropping when threshold exceeded
3. **Protocol Recovery**: Sequence ID resynchronization
4. **Application Recovery**: Callback-based error notifications

## Performance Optimization

### Buffer Management

```cpp
// Pre-allocated buffers
class BufferPool {
private:
    std::vector<std::vector<uint8_t>> buffers;
    std::mutex mutex;
    
public:
    std::vector<uint8_t> acquire(size_t size);
    void release(std::vector<uint8_t>&& buffer);
};
```

### Connection Pooling

```cpp
// Connection factory for creating TCP connections
class TcpConnectionFactory {
public:
    static TcpConnectionSp create(Socket socket);
    static void configureSocket(Socket socket, const ClientConfiguration& config);
};
```

### Performance Monitoring

```cpp
// Global performance counter
extern PerformanceCounter g_perfCounter;

// Function scope timing
#define PERF_SCOPE(name) \
    auto _perf_scope_start = std::chrono::high_resolution_clock::now(); \
    auto _perf_scope_name = name; \
    ScopeGuard _perf_guard([&]{ \
        auto _perf_scope_end = std::chrono::high_resolution_clock::now(); \
        g_perfCounter.add(_perf_scope_name, \
            std::chrono::duration_cast<std::chrono::microseconds>( \
                _perf_scope_end - _perf_scope_start).count()); \
    });
```

## Security Considerations

### Input Validation

```cpp
// Message size validation
bool isValidMessageSize(size_t size) {
    return size > 0 && size <= VC_MAX_DATA_PAYLOAD_SIZE;
}

// Packet checksum verification
bool verifyChecksum(const uint8_t* data, size_t length, uint16_t checksum) {
    uint16_t calculated = 0;
    for (size_t i = 0; i < length; i++) {
        calculated ^= data[i];
    }
    return calculated == checksum;
}
```

### Resource Protection

```cpp
// Rate limiting
class RateLimiter {
private:
    std::chrono::steady_clock::time_point lastUpdate;
    size_t bytesProcessed;
    size_t maxBytesPerSecond;
    
public:
    bool canProcess(size_t bytes);
    void update(size_t bytes);
};
```

## Configuration System

### Configuration Hierarchy

```
┌─────────────────────────────────────────────────────────────┐
│                   Configuration                           │
│  ┌─────────────┐ ┌─────────────┐ ┌─────────────┐         │
│  │ Default     │ │ File        │ │ Command     │         │
│  │ Values      │ │ Config      │ │ Line Args   │         │
│  └─────────────┘ └─────────────┘ └─────────────┘         │
│             ┌─────────────────────────────────────┐       │
│             │          Merged Configuration        │       │
│             └─────────────────────────────────────┘       │
└─────────────────────────────────────────────────────────────┘
```

### Configuration Classes

```cpp
// Base configuration
struct BaseConfiguration {
    int logLevel = INFO;
    bool enablePerfCounters = true;
    std::string logFile = "";
};

// Client-specific configuration
struct ClientConfiguration : BaseConfiguration {
    int localHostUdpPort = 5002;
    int peerTcpPort = 7001;
    std::string peerAddress = "localhost";
    // ... other client settings
};

// Server-specific configuration
struct ServerConfiguration : BaseConfiguration {
    int tcpPort = 7001;
    int maxConnections = 100;
    // ... other server settings
};
```

## Extension Points

### Plugin Architecture

```cpp
// Abstract protocol handler
class ProtocolHandler {
public:
    virtual bool handle(const Message& data) = 0;
    virtual ~ProtocolHandler() = default;
};

// Plugin registry
class ProtocolHandlerRegistry {
public:
    void registerHandler(const std::string& name, ProtocolHandler* handler);
    ProtocolHandler* getHandler(const std::string& name);
};
```

### Custom Data Processors

```cpp
// Custom data processor interface
class IDataProcessor {
public:
    virtual bool process(const Message& input, Message& output) = 0;
    virtual ~IDataProcessor() = default;
};

// Custom data processor registry
class DataProcessorRegistry {
public:
    void registerProcessor(const std::string& name, IDataProcessor* processor);
    IDataProcessor* getProcessor(const std::string& name);
};
```