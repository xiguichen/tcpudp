# TCP/UDP Project Documentation

## Overview

This project implements a sophisticated TCP/UDP communication system with virtual channel abstraction and high-performance networking capabilities. The system provides reliable UDP communication over multiple TCP connections, designed for scenarios where UDP is desired but TCP reliability is required.

## Table of Contents

1. [Getting Started](#getting-started)
2. [Architecture Overview](#architecture-overview)
3. [Virtual Channel System](#virtual-channel-system)
4. [Protocol Details](#protocol-details)
5. [Building the Project](#building-the-project)
6. [Running Tests](#running-tests)
7. [Usage Examples](#usage-examples)
8. [Configuration](#configuration)
9. [Development Guide](#development-guide)
10. [API Reference](#api-reference)

## Getting Started

### Prerequisites

- C++20 compatible compiler
- CMake 3.10 or higher
- Ninja build system (recommended)
- Python 3.x (for build and test scripts)
- Google Test framework

### Quick Setup

```bash
# Clone the repository
git clone <repository-url>
cd tcpudp

# Build the project
python build.py

# Run tests
python test.py
```

### First Test

1. Start the server:
```bash
Build/server/server
```

2. In another terminal, run the test client:
```bash
python src/test/client.py
```

## Architecture Overview

### System Architecture

```
┌─────────────────┐    TCP Connections    ┌─────────────────┐
│   Client App   │ ◄──────────────────► │   Server App   │
└─────────────────┘ (8 connections)    └─────────────────┘
       ▲                                        │
       │ UDP Traffic                             │ UDP Traffic
       │                                        ▼
┌─────────────────┐                   ┌─────────────────┐
│ Virtual Channel │                   │ Virtual Channel │
│ (Message-based) │                   │ (Message-based) │
└─────────────────┘                   └─────────────────┘
       ▲                                        │
       │ TCP Socket Layer                       │ TCP Socket Layer
       │                                        ▼
┌─────────────────┐                   ┌─────────────────┐
│   TCP/UDP      │                   │   TCP/UDP      │
│   Socket       │                   │   Socket       │
│   Abstraction  │                   │   Abstraction  │
└─────────────────┘                   └─────────────────┘
```

### Key Components

1. **Client**: Manages virtual channel preparation and connection lifecycle
2. **Server**: Listens for connections and manages peer tracking
3. **Virtual Channel**: Abstracts multiple TCP connections into a reliable message channel
4. **Protocol Layer**: Implements VC and UVT protocols for data framing
5. **Socket Abstraction**: Cross-platform TCP/UDP socket operations

## Virtual Channel System

### Virtual Channel Concept

The virtual channel system abstracts multiple TCP connections into a single logical channel for reliable message-based communication. This allows UDP-like semantics over TCP infrastructure.

### Key Features

- **Multiple TCP Connections**: Uses 8 concurrent TCP connections for parallel data transfer
- **Message-based Protocol**: Messages are reliably sequenced and delivered
- **Queue Management**: Per-connection queues prevent memory overload
- **Automatic Reconnection**: Exponential backoff retry mechanism

### Virtual Channel Flow

```
Client App                          Virtual Channel                          Server App
   │                                    │                                      │
   │ Send UDP Message                   │                                      │
   ├──────────────────────────────────►│                                      │
   │                                    │┌─────────────────┐                   │
   │                                    ││ TCP Connection 1│                   │
   │                                    │└─────────────────┘                   │
   │                                    │┌─────────────────┐                   │
   │                                    ││ TCP Connection 2│                   │
   │                                    │└─────────────────┘                   │
   │          ... 8 connections ...    │         ...                         │
   │                                    │┌─────────────────┐                   │
   │                                    ││ TCP Connection 8│                   │
   │                                    │└─────────────────┘                   │
   │                                    │                                      │
   │◄──────────────────────────────────┘                                      │
   │           UDP Message Delivered                                          │
   │                                    │                                      │
```

## Protocol Details

### VC Protocol (Virtual Channel)

Binary protocol for virtual channel communication:

```cpp
#pragma pack(push, 1)
struct VCHeader {
    uint8_t type;      // Packet type
    uint16_t msgId;    // Message ID
};

struct VCDataPacket {
    VCHeader header;
    uint16_t length;   // Data length
    uint8_t data[VC_MAX_DATA_PAYLOAD_SIZE]; // Payload (max 2000 bytes)
};
#pragma pack(pop)
```

### UVT Protocol (UDP over TCP)

Higher-level protocol for UDP data encapsulation:

```cpp
#pragma pack(push, 1)
struct UvtHeader {
    uint16_t size;      // Total packet size
    uint16_t msgId;     // Message ID
    uint16_t checksum;  // XOR checksum
};
#pragma pack(pop)
```

## Building the Project

### Build Commands

```bash
# Primary build (Ninja + CMake)
python build.py

# Alternative build (Make)
cd src && make compile

# Clean build
rm -rf Build/
python build.py
```

### Cross-Platform Support

**Windows:**
```bash
# Use Visual Studio Developer Command Prompt
python build.py
```

**Linux/macOS:**
```bash
# Standard build tools
python build.py
```

## Running Tests

### Test Execution

```bash
# Run all tests
python test.py

# Run specific test
Build/tests/CommonTest --gtest_filter=SuiteName.TestName

# Run with wildcard
Build/tests/CommonTest --gtest_filter=SuiteName.*

# List available tests
Build/tests/CommonTest --gtest_list_tests
```

### Test Categories

- **Unit Tests**: Located in `src/tests/`
- **Client Tests**: Located in `src/tests/client/`
- **Integration Tests**: Python scripts in `src/test/`

## Usage Examples

### Basic Server

```cpp
#include "Server.h"

int main() {
    Server server;
    
    // Start server with default configuration
    if (!server.start()) {
        log_error("Failed to start server");
        return 1;
    }
    
    log_info("Server started successfully");
    
    // Wait for connections
    while (server.isRunning()) {
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }
    
    return 0;
}
```

### Basic Client

```cpp
#include "Client.h"

int main() {
    Client client;
    
    // Connect to server
    if (!client.connect("20.2.117.216", 7001)) {
        log_error("Failed to connect to server");
        return 1;
    }
    
    // Send data through virtual channel
    client.sendUDPData("hello", 5);
    
    // Wait for response
    std::this_thread::sleep_for(std::chrono::seconds(1));
    
    return 0;
}
```

### Python Test Client

```python
import socket

def udp_client():
    server_address = ('localhost', 5002)
    
    with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as sock:
        # Send message
        message = b'hello'
        sock.sendto(message, server_address)
        
        # Receive response
        data, server = sock.recvfrom(4096)
        print(f'Received: {data}')

if __name__ == '__main__':
    udp_client()
```

## Configuration

### Client Configuration

File: `src/client/config.json`

```json
{
  "localHostUdpPort": 5002,
  "peerTcpPort": 7001,
  "peerAddress": "20.2.117.216",
  "clientId": 1
}
```

### Server Configuration

Configured programmatically in `ServerConfiguration.h/cpp`:

```cpp
struct ServerConfiguration {
    int tcpPort = 7001;
    int maxConnections = 100;
    int recvBufferSize = 65536;
    int sendBufferSize = 65536;
};
```

### Command Line Options

```bash
# Set log level
./server --log-level=DEBUG
./client --log-level=INFO

# Show help
./server --help
```

## Development Guide

### Code Style

- **Formatting**: Use `.clang-format` (Microsoft style, 4-space indent)
- **Naming**: PascalCase for classes, camelCase for members
- **Memory Management**: Use `std::shared_ptr`/`std::unique_ptr`
- **Includes**: Standard library → Third-party → Project headers

### Adding New Features

1. **Code First, Tests Second**: Implement functionality before writing tests
2. **Mock Dependencies**: Use mock classes for unit tests
3. **Follow Existing Patterns**: Adhere to established architecture
4. **Document Changes**: Update this documentation for new features

### Debugging Tips

- Enable debug logging: `--log-level=DEBUG`
- Check queue sizes when performance issues arise
- Monitor connection states with `VcManager`
- Use `PerformanceCounter` to measure function execution times

### Common Issues

1. **Connection Timeouts**: Check TCP connection timeout settings
2. **Queue Overflow**: Monitor `SEND_QUEUE_DROP_THRESHOLD`
3. **Memory Usage**: Review blocking queue sizes
4. **Protocol Errors**: Verify packet structure and checksums

## API Reference

### Client Class

```cpp
class Client {
public:
    // Connection
    bool connect(const std::string& address, int port);
    void disconnect();
    
    // UDP Operations
    void sendUDPData(const void* data, size_t length);
    void sendUDPData(const std::string& data);
    
    // Configuration
    void setConfiguration(const ClientConfiguration& config);
    
    // Status
    bool isConnected() const;
    int getClientId() const;
};
```

### Server Class

```cpp
class Server {
public:
    // Lifecycle
    bool start();
    void stop();
    bool isRunning() const;
    
    // Connection Management
    int getActiveConnectionCount() const;
    void broadcast(const void* data, size_t length);
};
```

### VirtualChannel Class

```cpp
class VirtualChannel {
public:
    // Message Handling
    void send(const Message& message);
    Message receive(int timeoutMs);
    
    // Callbacks
    void setReceiveCallback(ReceiveCallback callback);
    void setDisconnectCallback(DisconnectCallback callback);
};
```

### Socket Utilities

```cpp
// Cross-platform socket operations
SocketResult send(Socket socket, const void* data, size_t length);
SocketResult recv(Socket socket, void* buffer, size_t length);

// Non-blocking I/O with timeouts
bool IsSocketReadable(Socket socket, int timeoutMs = 100);
bool IsSocketWritable(Socket socket, int timeoutMs = 100);
```

## Performance Considerations

### Tuning Parameters

1. **Buffer Sizes**: Adjust `recvBufferSize` and `sendBufferSize` in configuration
2. **Connection Count**: Tune `VC_TCP_CONNECTIONS` based on available bandwidth
3. **Queue Thresholds**: Monitor `SEND_QUEUE_DROP_THRESHOLD` to prevent memory issues
4. **Timeouts**: Optimize TCP connection and operation timeouts

### Best Practices

1. **Batch Small Messages**: Combine multiple small messages when possible
2. **Monitor Performance**: Use `g_perfCounter` to track bottlenecks
3. **Graceful Shutdown**: Properly close all connections and clean up resources
4. **Error Handling**: Implement proper error recovery mechanisms

## Troubleshooting

### Common Problems

1. **Connection Refused**
   - Check server is running on specified port
   - Verify firewall settings
   - Confirm correct IP address

2. **High Latency**
   - Check network bandwidth
   - Monitor queue sizes
   - Consider increasing connection count

3. **Memory Issues**
   - Monitor `SEND_QUEUE_DROP_THRESHOLD`
   - Check for memory leaks in custom code
   - Review buffer sizes

4. **Protocol Errors**
   - Verify packet structure
   - Check endianness
   - Validate checksums

### Debug Commands

```bash
# Monitor running processes
ps aux | grep -E "(server|client)"

# Check network connections
netstat -an | grep :7001
lsof -i :7001

# Memory usage
pmap <pid>

# CPU usage
top -p <pid>
```

## Contributing

### Development Workflow

1. **Before Changes**: Ensure tests compile and pass
2. **Make Changes**: Implement with proper error handling
3. **Test Changes**: Run full test suite
4. **Document**: Update relevant documentation
5. **Submit PR**: Include justification for changes

### Code Review Checklist

- [ ] Tests pass
- [ ] Code follows style guidelines
- [ ] Memory management is correct
- [ ] Error handling is comprehensive
- [ ] Documentation is updated

## License

[Add license information here]