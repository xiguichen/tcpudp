cmake_minimum_required(VERSION 3.10)

project(Client)

set(CMAKE_CXX_STANDARD 20)


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

# Add common library as dependency
target_link_libraries(client common)

# Add common library as include directory
target_include_directories(client PRIVATE ${CMAKE_SOURCE_DIR}/common)


# Link libraries if needed
# target_link_libraries(client <libraries>)

# Copy config.json to the build directory
configure_file(config.json config.json COPYONLY)

set_target_properties(client PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
