# Copilot Instructions for tcpudp

## Project Overview
UDP-over-TCP tunneling application using virtual channels. Enables UDP traffic traversal through NAT via reliable TCP connections. Cross-platform C++20 (Windows/Linux).

## Architecture

### Module Structure
- **`src/common/`** - Shared networking primitives, threading, and VC abstraction
- **`src/client/`** - NAT-behind client: captures local UDP → forwards over TCP VirtualChannel
- **`src/server/`** - Public server: receives TCP → relays to destination UDP

### Key Abstractions
| Class | Purpose |
|-------|---------|
| `VirtualChannel` | Abstract interface for reliable channels (see [VirtualChannel.h](src/common/VirtualChannel.h)) |
| `TcpVirtualChannel` | Concrete impl with 4 TCP connections per channel, message ordering via atomic counters |
| `TcpConnection` | Single TCP socket wrapper with send timeout (1.8s) |
| `BlockingQueue` | Thread-safe producer-consumer queue for async data flow |
| `StopableThread` | Base class for stoppable worker threads |

### Threading Model
Each `TcpVirtualChannel` spawns:
- N `TcpVCReadThread` instances (one per TCP connection)
- N `TcpVCWriteThread` instances sharing a single `BlockingQueue`

Threads inherit from `StopableThread` - always call `stop()` before destruction.

### Protocol
- **UVT Protocol** ([Protocol.h](src/common/Protocol.h)): Length-prefixed UDP packets with XOR checksum
- **VC Protocol** ([VcProtocol.h](src/common/VcProtocol.h)): Message ID ordering, 2000-byte max payload
- Client sends `MsgBind` with `clientId` immediately after TCP connect

## Build & Test

```bash
# Build (from repo root) - uses Ninja
python build.py          # or python3 build.py

# Alternative (from src/)
make compile

# Run tests (Windows)
test.cmd                  # runs Build\tests\CommonTest.exe

# Run tests (Linux)
cd Build && tests/CommonTest
```

**Important**: Always build after modifying C++/CMake files to verify compilation. Never run `python run.py` (runs indefinitely).

## Code Conventions

### Formatting
- Microsoft style via `.clang-format`: 4-space indent, no tabs
- Run formatter before committing

### Naming Patterns
- Shared pointers: `TypeSp` suffix (e.g., `TcpConnectionSp`, `BlockingQueueSp`)
- Configuration: Singleton pattern via `getInstance()` (see `ClientConfiguration`, `ServerConfiguration`)

### Socket Abstraction
Use platform wrappers from [Socket.h](src/common/Socket.h):
```cpp
SocketFd sock = SocketCreate(AF_INET, SOCK_STREAM, 0);
SendTcpData(sock, data, len, 0);
RecvTcpData(sock, buffer, bufSize, 0);
SocketClose(sock);
```
Non-blocking variants: `*NonBlocking()` functions with timeout parameter.

### Logging
Use macros from [Log.h](src/common/Log.h):
```cpp
log_debug("detailed trace");
log_info(std::format("Connected to {}:{}", ip, port));
log_warnning("recoverable issue");  // note: typo is intentional
log_error("fatal condition");
```

### Thread Safety
- Use `std::mutex` + `std::lock_guard` for shared state
- Prefer `std::atomic<>` for simple flags (see `StopableThread::running`)
- `BlockingQueue` handles its own synchronization

## Configuration

### Client ([src/client/config.json](src/client/config.json))
```json
{
  "localHostUdpPort": 5002,
  "peerTcpPort": 7001,
  "peerAddress": "server-ip",
  "clientId": 1
}
```

### Server
Port configured via `ServerConfiguration::getPortNumber()` (default 7001).

## Testing
- Google Test framework, fetched via CMake `FetchContent`
- Tests in `src/tests/` - add new test files, they're auto-discovered via `GLOB_RECURSE`
- Mock pattern example: [MockSocket.h](src/tests/client/MockSocket.h)
