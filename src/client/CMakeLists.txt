cmake_minimum_required(VERSION 3.10)

project(Client)

set(CMAKE_CXX_STANDARD 17)

# Add source files
set(SOURCES
    main.cpp
    Client.cpp
    SocketManager.cpp
    LocalUdpSocket.cpp
    PeerTcpSocket.cpp
    UdpToTcpQueue.cpp
    TcpToUdpQueue.cpp
)


# Add executable
add_executable(client ${SOURCES})

# Add include directories
include_directories(${CMAKE_SOURCE_DIR}/third_party/json)

# Link libraries if needed
# target_link_libraries(client <libraries>)
