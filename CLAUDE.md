# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Build System

### Build Commands
- **Primary**: `python build.py` (from repo root, uses Ninja)
- **Alternative**: `make compile` (from src/, if using Make)
- **Clean**: Delete `Build/` directory and rebuild

### Cross-Platform
- Windows: Use Visual Studio Developer Command Prompt for proper environment
- Linux/macOS: Use standard build tools (Ninja or Make)

## Testing

### Test Execution
- **Full suite**: `python test.py`
- **Single test**:
  - Windows: `Build/tests/CommonTest.exe --gtest_filter=SuiteName.TestName`
  - Linux/macOS: `Build/tests/CommonTest --gtest_filter=SuiteName.TestName`
- **Wildcards**: `--gtest_filter=SuiteName.*` or `--gtest_filter=*TestName`
- **List tests**: `Build/tests/CommonTest --gtest_list_tests`

### Test Location
- Test binaries: `Build/tests/` (auto-discovered by CMake)
- Test source: `src/tests/` and `src/tests/client/`

## Code Architecture

### Project Structure
```
src/
├── client/        # TCP/UDP client implementation
├── server/        # TCP/UDP server implementation
├── common/        # Shared utilities and abstractions
├── tests/         # Unit tests
├── test_vc_client/# Virtual channel test client
├── third_party/   # Third-party dependencies (nlohmann/json)
└── CMakeLists.txt # CMake build configuration
```

### Key Architectural Patterns

**Virtual Channel (VC) Architecture**
- Virtual channels abstract TCP connections using a message-based protocol
- Each VC uses multiple TCP connections (default: 2) for reliability and throughput
- Message-based protocol (VCDataPacket) with sequence IDs and max payload of 2000 bytes
- Managed by singleton `VcManager` to track active channels by client ID

**Non-Blocking I/O**
- All socket operations use non-blocking I/O with configurable timeouts
- Timeout-based polling via `IsSocketReadable()` and `IsSocketWritable()`
- Socket abstraction layer in `Socket.h/cpp` provides cross-platform support (Windows/Linux/macOS)
- Error codes: `SOCKET_ERROR_NONE`, `SOCKET_ERROR_WOULD_BLOCK`, `SOCKET_ERROR_TIMEOUT`, etc.

**Threading Model**
- Separate read/write threads for each TCP connection in a VC
- Use `BlockingQueue` for thread-safe data transfer between threads
- `StopableThread` base class for graceful thread shutdown
- `TcpVCReadThread` and `TcpVCWriteThread` handle individual connection I/O

**Protocol**
- Binary protocol defined in `VcProtocol.h` using packed structures
- VCHeader contains packet type and message ID
- VCDataPacket includes data length and payload
- Uses `#pragma pack(push, 1)` for 1-byte alignment

### Core Components

**Socket Layer** (`src/common/Socket.h/cpp`)
- Cross-platform socket abstractions (Windows SOCKET vs Linux int)
- Non-blocking I/O with timeouts
- Polling functions for event-driven I/O
- Send/Receive functions for TCP and UDP

**Virtual Channel** (`src/common/VirtualChannel.h/cpp`, `TcpVirtualChannel.h/cpp`)
- Abstract base class defining channel interface
- `TcpVirtualChannel` implements with multiple TCP connections
- Message queue for received data (`receivedDataMap`)
- Callbacks for receive and disconnect events

**TCP Connection** (`src/common/TcpConnection.h/cpp`)
- Individual TCP connection with timeout (1.8s send timeout)
- Atomic connection state tracking
- Disconnect callbacks

**Client** (`src/client/Client.h/cpp`)
- Manages TCP and UDP sockets
- Prepares virtual channel for communication
- Handles connection lifecycle

**Server** (`src/server/Server.h/cpp`)
- Listens for incoming connections
- Accepts connections and manages peer state
- Uses `VcManager` to track active virtual channels

## Code Style

### Formatting
- Use `.clang-format` (BasedOnStyle: Microsoft, 4-space indent, no tabs)
- Run: `clang-format -i **/*.cpp **/*.hpp`

### Naming Conventions
- Classes: PascalCase (e.g., `TcpVirtualChannel`)
- Members: camelCase (e.g., `receiveCallback`)
- Shared pointers: `TypeSp` suffix (e.g., `TcpConnectionSp`)
- Singletons: `getInstance()` pattern

### Memory Management
- `std::shared_ptr` for shared ownership
- `std::unique_ptr` for exclusive ownership
- Use type aliases: `using FooSp = std::shared_ptr<Foo>;`

### Includes Order
1. Standard library
2. Third-party
3. Project headers

Use `<>` for system headers, `""` for local headers.

### Logging
Use `Log.h` macros: `log_debug`, `log_info`, `log_warning`, `log_error`
- Logging macros include file/line information
- Set log level via command line: `--log-level=DEBUG|INFO|WARNING|ERROR`

## Testing Guidelines

### Test Structure
- Location: `src/tests/` and `src/tests/client/`
- Framework: Google Test (gtest)
- Mock classes for isolation (e.g., `MockSocket.h`)

### Writing Tests
1. Follow existing test patterns in `src/tests/`
2. Use mocks for dependencies
3. Test error cases, not just success paths
4. Keep tests focused and independent

### Example
```cpp
TEST(SuiteName, TestName) {
    // Arrange
    MockSocket mock;
    // Act
    bool result = mock.doSomething();
    // Assert
    EXPECT_TRUE(result);
}
```

## Important Implementation Details

### Thread Safety
- Mutexes for shared state (e.g., `vcsMutex` in VcManager)
- `std::atomic` for simple flags (e.g., `connected`, `opened`)
- Lock guards (`std::lock_guard`) for mutex protection

### Error Handling
- Check return codes from socket operations
- Handle `SOCKET_ERROR_WOULD_BLOCK` as transient errors
- Handle `SOCKET_ERROR_TIMEOUT` appropriately
- Log errors with context using Log macros

### Signal Handling
- Server: Handles SIGINT, SIGTERM, SIGHUP with graceful shutdown
- Client: Handles signals for clean exit and cleanup
- Socket cleanup on termination

### Configuration
- Client config: `src/client/config.json`
- Server config: `src/server/ServerConfiguration.h/cpp`
- Command-line options: `--log-level=LEVEL`, `--help`

## Development Workflow

1. **Before changes**: Ensure tests compile in target environment
2. **After changes**: `python build.py` → `python test.py` → open PR
3. **PR content**: Include justification (why, not just what)

### Quick Verification
```bash
python build.py
Build/tests/CommonTest --gtest_filter=TestName
python test.py
```
