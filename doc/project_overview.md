# Project Overview

This project is a C++ application designed to facilitate communication over TCP/IP networks using virtual channels. It includes both server and client components, with support for reliable data transmission, connection management, and extensibility for custom data processing.

## Key Components

### Core Classes

1. **VirtualChannel**
   - An abstract base class defining the interface for virtual channels.
   - Provides methods for opening, sending data, checking status, closing, and setting receive callbacks.

2. **TcpVirtualChannel**
   - Implements the `VirtualChannel` interface for TCP-based communication.
   - Manages multiple connections, read/write threads, and ensures ordered message delivery.
   - Uses atomic counters for message IDs to maintain data integrity.

3. **TcpConnection**
   - Encapsulates a single TCP connection.
   - Handles sending and receiving data, connection status, and disconnection.

4. **Socket Utilities**
   - Defined in `Socket.h`, these utilities provide cross-platform socket operations.
   - Includes functions for socket creation, configuration, and I/O operations.

5. **Server**
   - Manages the server-side operations, including listening for connections and accepting clients.
   - Coordinates the setup of virtual channels for connected clients.

6. **Client**
   - Handles client-side operations, establishing connections to the server.
   - Prepares virtual channels and UDP sockets for communication.

7. **BlockingQueue**
   - A thread-safe queue implementation for managing data between threads.
   - Used for buffering outgoing data in virtual channels.

8. **DataProcessorThread**
   - Dedicated thread for processing incoming data.
   - Works with `IDataReader` and `IDataProcessor` interfaces for customizable data handling.

## Architecture Highlights

- **Modular Design:** Clear separation of concerns with distinct classes for networking, data processing, and application logic.
- **Thread Safety:** Extensive use of mutexes and atomic variables to ensure safe concurrent access.
- **Cross-Platform Compatibility:** Socket abstractions enable operation on both Windows and Unix-like systems.
- **Extensibility:** Interfaces like `IDataReader` and `IDataProcessor` allow customization of data handling mechanisms.

## Usage

- The server listens for incoming connections and manages virtual channels for each client.
- Clients connect to the server, prepare virtual channels, and exchange data.
- Data is transmitted reliably with mechanisms for ordering and duplicate detection.

