cmake_minimum_required(VERSION 3.10)

project(Client)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED True)

# Set the build type to Debug by default if not specified
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Debug)
endif()

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
add_executable(udp_client ${SOURCES})

# Add include directories
target_include_directories(udp_client PRIVATE ${CMAKE_SOURCE_DIR}/third_party/json)

# Add common library as dependency
target_link_libraries(udp_client common)

# Add common library as include directory
target_include_directories(udp_client PRIVATE ${CMAKE_SOURCE_DIR}/common)

# Copy config.json to the build directory
configure_file(config.json config.json COPYONLY)

set_target_properties(udp_client PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

# Enable /EHsc and generate PDB files for Windows platform
if (WIN32)
    target_compile_options(udp_client PRIVATE /EHsc /Zi)
    set_target_properties(udp_client PROPERTIES COMPILE_PDB_NAME "udp_client")
    set_target_properties(udp_client PROPERTIES COMPILE_PDB_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/pdb")
    set_target_properties(udp_client PROPERTIES LINK_FLAGS "/DEBUG")
endif()
