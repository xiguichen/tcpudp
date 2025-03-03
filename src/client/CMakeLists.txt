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
target_include_directories(client PRIVATE ${CMAKE_SOURCE_DIR}/third_party/json)

# Add Common library as dependency
target_link_libraries(client common)

# Add Common library as include directory
target_include_directories(client PRIVATE ${CMAKE_SOURCE_DIR}/Common)


# Link libraries if needed
# target_link_libraries(client <libraries>)

# Copy config.json to the build directory
configure_file(config.json config.json COPYONLY)
