# Development Guide

## Table of Contents

1. [Development Environment Setup](#development-environment-setup)
2. [Code Organization](#code-organization)
3. [Building and Testing](#building-and-testing)
4. [Debugging Techniques](#debugging-techniques)
5. [Adding New Features](#adding-new-features)
6. [Performance Optimization](#performance-optimization)
7. [Code Review Checklist](#code-review-checklist)
8. [Common Issues and Solutions](#common-issues-and-solutions)

## Development Environment Setup

### Prerequisites

- **Compiler**: C++20 compatible (GCC 10+, Clang 12+, MSVC 19.29+)
- **Build System**: CMake 3.10+, Ninja 1.8+ (recommended)
- **Testing**: Google Test 1.10+
- **Formatting**: clang-format 10+
- **Version Control**: Git

### IDE Setup

#### Visual Studio (Windows)
```bash
# Install Visual Studio with C++ development tools
# Clone repository
git clone <repository-url>
cd tcpudp

# Open solution
tcpudp.sln
```

#### VS Code (Cross-platform)
```bash
# Install VS Code extensions
# C/C++, CMake Tools, CMake Tools Kit, Git Graph

# Create workspace settings
mkdir .vscode
cat > .vscode/settings.json << EOF
{
    "cmake.configureOnOpen": true,
    "cmake.generator": "Ninja",
    "cmake.buildDirectory": "Build",
    "files.associations": {
        "*.h": "cpp",
        "*.hpp": "cpp",
        "*.cpp": "cpp"
    },
    "clang-format.executable": "${workspaceFolder}/.clang-format"
}
EOF

# Create tasks.json for build and test
cat > .vscode/tasks.json << EOF
{
    "version": "2.0.0",
    "tasks": [
        {
            "label": "build",
            "type": "shell",
            "command": "python build.py",
            "group": "build"
        },
        {
            "label": "test",
            "type": "shell",
            "command": "python test.py",
            "group": "test"
        }
    ]
}
EOF
```

### Environment Configuration

#### Local Development Configuration

```bash
# Create local config directory
mkdir -p ~/.config/tcpudp

# Create environment file
cat > ~/.config/tcpudp/development.env << EOF
# Build configuration
CMAKE_BUILD_TYPE=Debug
ENABLE_TESTS=ON
LOG_LEVEL=DEBUG

# Network configuration
DEFAULT_SERVER_PORT=7001
DEFAULT_CLIENT_PORT=5002

# Performance settings
VC_CONNECTIONS=8
QUEUE_THRESHOLD=500
EOF
```

## Code Organization

### Directory Structure

```
src/
├── client/                # Client implementation
│   ├── Client.cpp         # Main client logic
│   ├── Client.h           # Client interface
│   ├── ClientConfiguration.cpp
│   ├── ClientConfiguration.h
│   └── config.json        # Client config
├── server/                # Server implementation
│   ├── Server.cpp         # Main server logic
│   ├── Server.h           # Server interface
│   ├── ServerConfiguration.cpp
│   ├── ServerConfiguration.h
│   ├── Peer.cpp           # Peer management
│   ├── Peer.h
│   └── PeerManager.cpp    # Peer tracking
├── common/                # Shared utilities
│   ├── Socket.cpp         # Socket abstraction
│   ├── Socket.h
│   ├── VirtualChannel.cpp
│   ├── VirtualChannel.h
│   ├── TcpConnection.cpp
│   ├── TcpConnection.h
│   ├── VcProtocol.h       # VC protocol definitions
│   ├── Protocol.h         # UVT protocol definitions
│   ├── DataProcessorThread.h
│   ├── QueueManager.cpp
│   ├── QueueManager.h
│   └── Factories/         # Factory classes
├── tests/                 # Unit tests
│   ├── CommonTest.cpp     # Main test suite
│   ├── Socket_Test.cpp
│   ├── VirtualChannel_Test.cpp
│   └── Protocol_Test.cpp
├── test_vc_client/        # Virtual channel test client
└── test/                  # Python integration tests
    ├── client.py
    └── server.py
```

### Code Style

#### Formatting

Use the provided `.clang-format` configuration:

```bash
# Format all C++ files
clang-format -i **/*.cpp **/*.hpp
```

#### Naming Conventions

```cpp
// Classes: PascalCase
class TcpVirtualChannel;
class VirtualChannelFactory;
class QueueManager;

// Members: camelCase
bool isConnected;
int connectionCount;
std::string peerAddress;

// Functions: camelCase
bool connect();
void disconnect();
int getActiveConnections();

// Constants: UPPER_CASE
const int MAX_CONNECTIONS = 8;
const int MAX_DATA_SIZE = 2000;

// Shared pointers: TypeSp suffix
using TcpConnectionSp = std::shared_ptr<TcpConnection>;
```

#### Include Order

```cpp
// 1. Standard library
#include <vector>
#include <string>
#include <memory>

// 2. Third-party
#include <nlohmann/json.hpp>

// 3. Project headers
#include "Socket.h"
#include "VirtualChannel.h"

// 4. System headers (only if needed)
#ifdef _WIN32
#include <winsock2.h>
#endif
```

#### Memory Management

```cpp
// Use smart pointers for ownership
class TcpVirtualChannel {
private:
    std::vector<TcpConnectionSp> connections;  // Shared ownership
    std::unique_ptr<QueueManager> queueMgr;   // Exclusive ownership
};

// Type aliases for cleaner code
using Message = std::vector<uint8_t>;
using ClientId = int;
```

## Building and Testing

### Build Process

```bash
# Clean build
rm -rf Build/
python build.py

# Build with verbose output
python build.py --verbose

# Build specific target
cd src && make compile

# Install (if applicable)
make install
```

### Testing

```bash
# Run all tests
python test.py

# Run specific test
Build/tests/CommonTest --gtest_filter=SuiteName.TestName

# Run with output to file
Build/tests/CommonTest --gtest_output=xml:test_results.xml

# Run tests with valgrind (Linux)
valgrind --leak-check=full Build/tests/CommonTest
```

### Continuous Integration

The project includes GitHub Actions for CI:

```yaml
# .github/workflows/ci.yml
name: CI

on: [push, pull_request]

jobs:
  build:
    runs-on: ubuntu-latest
    steps:
    - uses: actions/checkout@v2
    - name: Build
      run: python build.py
    - name: Test
      run: python test.py
```

## Debugging Techniques

### Logging

```cpp
// Use logging macros
#include "Log.h"

log_debug("Debug info: connection=%d, size=%zu", connId, size);
log_info("Client connected from %s", address.c_str());
log_warning("Queue size approaching threshold: %d", queueSize);
log_error("Connection failed: %s", errorMessage.c_str());

// Set log level via environment
export LOG_LEVEL=DEBUG

// Custom log formatter
void customLog(LogLevel level, const char* file, int line, 
               const char* function, const char* message) {
    // Custom formatting logic
}
```

### Performance Debugging

```cpp
// Use performance counters
#include "PerformanceCounter.h"

// Track function performance
{
    PERF_SCOPE("sendUDPData");
    // Function code
}

// Global performance counter
extern PerformanceCounter g_perfCounter;

// Print performance stats
g_perfCounter.printStatistics();
```

### Debug Builds

```bash
# Debug build with symbols
python build.py --build-type Debug

# Release build with debug info
python build.py --build-type RelWithDebInfo

# Profile build
python build.py --build-type Release -DCMAKE_CXX_FLAGS=-g
```

### Memory Debugging

```cpp
// Memory leak detection (Linux)
valgrind --leak-check=full --show-leak-kinds=all ./server

// Address sanitizer
python build.py -DCMAKE_CXX_FLAGS="-fsanitize=address -g"

# Thread sanitizer
python build.py -DCMAKE_CXX_FLAGS="-fsanitize=thread -g"
```

### Network Debugging

```cpp
// Enable network logging
log_setNetworkLogLevel(DEBUG);

// Print socket information
void logSocketInfo(Socket socket, const std::string& operation) {
    log_debug("Socket %s: fd=%d, error=%d", 
             operation.c_str(), socket, getLastError());
}

// Network statistics
void printNetworkStats() {
    log_info("Active connections: %d", getActiveConnectionCount());
    log_info("Total bytes sent: %zu", getTotalBytesSent());
    log_info("Total bytes received: %zu", getTotalBytesReceived());
}
```

## Adding New Features

### Feature Development Process

1. **Planning**: Create feature specification and API design
2. **Implementation**: Write code following existing patterns
3. **Testing**: Write comprehensive unit tests
4. **Documentation**: Update relevant documentation
5. **Review**: Submit for code review

### Adding New Protocol Support

```cpp
// 1. Create protocol handler
class CustomProtocol {
public:
    virtual bool handle(const Message& data) = 0;
    virtual ~CustomProtocol() = default;
};

// 2. Register protocol
class ProtocolRegistry {
public:
    static void register(const std::string& name, std::unique_ptr<CustomProtocol> handler);
    static CustomProtocol* get(const std::string& name);
};

// 3. Update protocol dispatch
bool TcpVirtualChannel::send(const Message& message) {
    // Protocol-specific handling
    if (auto handler = ProtocolRegistry::get("custom")) {
        return handler->handle(message);
    }
    // Default handling
    return defaultSend(message);
}
```

### Adding New Data Processor

```cpp
// 1. Implement processor interface
class CustomDataProcessor : public IDataProcessor {
public:
    bool process(const Message& input, Message& output) override {
        // Custom processing logic
        // ...
        return true;
    }
};

// 2. Register processor
DataProcessorRegistry::register("custom", 
    std::make_unique<CustomDataProcessor>());

// 3. Use in processing pipeline
Message processData(const Message& input) {
    Message output;
    if (auto processor = DataProcessorRegistry::get("custom")) {
        processor->process(input, output);
    }
    return output;
}
```

### Adding New Configuration Options

```cpp
// 1. Add to configuration structure
struct ExtendedClientConfiguration : ClientConfiguration {
    int customOption = 100;
    bool enableFeature = false;
    std::string customParam = "default";
};

// 2. Update configuration loader
void loadExtendedConfig(const std::string& filename, 
                       ExtendedClientConfiguration& config) {
    // Load basic config
    loadBasicConfig(filename, config);
    
    // Load extended options
    nlohmann::json json;
    std::ifstream file(filename);
    file >> json;
    
    if (json.contains("customOption")) {
        config.customOption = json["customOption"];
    }
    // ... other options
}
```

## Performance Optimization

### Profiling

```bash
# Build with profiling flags
python build.py -DCMAKE_CXX_FLAGS="-pg"

# Run application
./server

# Generate profile
gprof ./server gmon.out > profile.txt

# Use perf for Linux profiling
perf record ./server
perf report
```

### Optimization Techniques

#### 1. Buffer Pooling

```cpp
class BufferPool {
private:
    std::vector<std::vector<uint8_t>> buffers;
    std::mutex mutex;
    
public:
    std::vector<uint8_t> acquire(size_t size) {
        std::lock_guard<std::mutex> lock(mutex);
        auto it = std::find_if(buffers.begin(), buffers.end(),
            [size](const auto& buf) { return buf.capacity() >= size; });
        
        if (it != buffers.end()) {
            std::vector<uint8_t> result = std::move(*it);
            buffers.erase(it);
            result.resize(size);
            return result;
        }
        
        return std::vector<uint8_t>(size);
    }
    
    void release(std::vector<uint8_t>&& buffer) {
        std::lock_guard<std::mutex> lock(mutex);
        buffer.clear();
        buffers.push_back(std::move(buffer));
    }
};
```

#### 2. Connection Multiplexing

```cpp
class ConnectionPool {
private:
    std::vector<TcpConnectionSp> connections;
    std::atomic<int> nextIndex{0};
    
public:
    TcpConnectionSp getNextConnection() {
        int index = nextIndex.fetch_add(1) % connections.size();
        return connections[index];
    }
    
    void addConnection(TcpConnectionSp conn) {
        connections.push_back(conn);
    }
};
```

#### 3. Zero-copy Operations

```cpp
// Use span for zero-copy data passing
void processData(std::span<const uint8_t> data) {
    // Process data without copying
    for (auto byte : data) {
        // Process byte
    }
}

// Send with span
bool sendUDPData(std::span<const uint8_t> data) {
    // Use span directly in send operation
    return socket.send(data.data(), data.size());
}
```

### Memory Usage Optimization

```cpp
// Pre-allocate frequently used sizes
class FixedSizeAllocator {
private:
    std::array<std::vector<uint8_t>, 16> pools;  // Sizes 16-256
    
public:
    std::vector<uint8_t> allocate(size_t size) {
        if (size <= 256) {
            int index = static_cast<int>(size) - 16;
            if (index >= 0 && index < 16 && !pools[index].empty()) {
                auto result = std::move(pools[index]);
                pools[index].clear();
                return result;
            }
        }
        return std::vector<uint8_t>(size);
    }
    
    void deallocate(std::vector<uint8_t>&& buffer) {
        size_t size = buffer.capacity();
        if (size >= 16 && size <= 256) {
            int index = static_cast<int>(size) - 16;
            if (index >= 0 && index < 16) {
                buffer.clear();
                pools[index].push_back(std::move(buffer));
            }
        }
    }
};
```

## Code Review Checklist

### Code Quality

- [ ] **Code Style**: Follows `.clang-format` guidelines
- [ ] **Naming**: Uses consistent naming conventions
- [ ] **Comments**: Complex logic is well-documented
- [ ] **Error Handling**: All error cases are handled
- [ ] **Memory Management**: Smart pointers used correctly

### Architecture

- [ ] **Design Patterns**: Uses appropriate design patterns
- [ ] **Separation of Concerns**: Each class has single responsibility
- [ ] **Abstraction Levels**: Proper abstraction between layers
- [ ] **Extensibility**: Code allows for future extensions
- [ ] **Dependencies**: Minimal and clear dependencies

### Performance

- [ ] **Resource Management**: Resources are properly freed
- [ ] **Locking**: Minimal and efficient locking
- [ ] **Memory Usage**: No unnecessary memory allocations
- [ ] **CPU Usage**: Optimal algorithms used
- [ ] **I/O Operations**: Non-blocking I/O used where appropriate

### Testing

- [ ] **Unit Tests**: Comprehensive test coverage
- [ ] **Integration Tests**: Tests verify integration between components
- [ ] **Edge Cases**: Tests cover edge cases and error conditions
- [ ] **Performance Tests**: Includes performance benchmarks if needed
- [ ] **Mock Objects**: Mock objects used for isolated testing

### Documentation

- [ ] **API Documentation**: Public APIs are documented
- [ ] **Architecture Decisions**: Key decisions are documented
- [ ] **Examples**: Usage examples provided
- [ ] **README**: Updated with new features
- [ ] **Change Log**: Changes are logged appropriately

### Security

- [ ] **Input Validation**: All external inputs are validated
- [ ] **Buffer Overflow**: No risk of buffer overflow
- [ ] **Information Disclosure**: No sensitive information leaked
- [ ] **Access Control**: Proper access controls in place
- [ ] **Logging**: Sensitive data not logged

## Common Issues and Solutions

### Build Issues

#### "undefined reference to 'main'"
```bash
# Solution: Link against the correct libraries
# CMakeLists.txt should have target_link_libraries
```

#### "fatal error: nlohmann/json.hpp not found"
```bash
# Solution: Third-party headers are in third_party/json/nlohmann/
# Update include path or copy to include directory
```

#### "undefined reference to `g_perfCounter`"
```bash
# Solution: Define the global variable in one source file
// In PerformanceCounter.cpp:
PerformanceCounter g_perfCounter;
```

### Runtime Issues

#### "Connection refused"
```cpp
// Solution: Check server is running and port is correct
bool checkServerConnection(const std::string& address, int port) {
    Socket sock = createTCPSocket();
    if (!connectSocket(sock, address, port)) {
        log_error("Connection failed: %s:%d", address.c_str(), port);
        return false;
    }
    close(sock);
    return true;
}
```

#### "Queue overflow"
```cpp
// Solution: Monitor queue sizes and adjust threshold
void monitorQueues() {
    for (auto& queue : getAllQueues()) {
        if (queue.size() > QUEUE_THRESHOLD * 0.8) {
            log_warning("Queue size approaching threshold: %zu", queue.size());
        }
    }
}
```

#### "High memory usage"
```cpp
// Solution: Implement buffer pooling and size limits
class QueueManager {
private:
    size_t maxQueueSize = 1000;  // Limit queue size
    
public:
    bool push(const Message& msg) {
        if (queue.size() >= maxQueueSize) {
            log_error("Queue full, dropping message");
            return false;
        }
        queue.push(msg);
        return true;
    }
};
```

### Performance Issues

#### "Slow message processing"
```cpp
// Solution: Optimize data processing pipeline
void processDataBatch(const std::vector<Message>& messages) {
    // Process multiple messages at once
    for (const auto& batch : chunkMessages(messages, 100)) {
        processBatch(batch);
    }
}
```

#### "High CPU usage"
```cpp
// Solution: Add backpressure and throttling
class Throttler {
private:
    std::chrono::steady_clock::time_point lastCheck;
    size_t bytesProcessed = 0;
    size_t maxBytesPerSecond = 1024 * 1024;  // 1 MB/s
    
public:
    bool canSend(size_t bytes) {
        auto now = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(
            now - lastCheck).count();
        
        if (elapsed >= 1) {
            bytesProcessed = 0;
            lastCheck = now;
        }
        
        if (bytesProcessed + bytes > maxBytesPerSecond) {
            return false;
        }
        
        bytesProcessed += bytes;
        return true;
    }
};
```

### Debugging Tips

#### Debug Logs Not Showing
```bash
# Check log level setting
export LOG_LEVEL=DEBUG

# Verify log configuration
log_setLogLevel(DEBUG);
```

#### Thread Deadlock
```cpp
// Solution: Use lock ordering and timeouts
std::mutex mutex1, mutex2;

void safeLock() {
    std::lock(mutex1, mutex2);  // Deadlock-free locking
    std::lock_guard<std::mutex> lock1(mutex1, std::adopt_lock);
    std::lock_guard<std::mutex> lock2(mutex2, std::adopt_lock);
}
```

#### Memory Leak
```cpp
// Solution: Use RAII and smart pointers
class ResourceGuard {
    Resource* resource;
    
public:
    ResourceGuard(Resource* r) : resource(r) {}
    ~ResourceGuard() { delete resource; }
    Resource* get() { return resource; }
};
```