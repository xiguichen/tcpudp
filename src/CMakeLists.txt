cmake_minimum_required(VERSION 3.10)
project(tcpudp)

# export json for lsp
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Prevent Windows min/max macros from conflicting with C++ std::min/std::max
if(WIN32)
  add_definitions(-DNOMINMAX -D_WINSOCKAPI_)
  add_compile_options(/wd4005)
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED True)

add_subdirectory(common)
add_subdirectory(client)
add_subdirectory(server)

