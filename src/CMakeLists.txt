cmake_minimum_required(VERSION 3.28)
project(tcpudp)

# export json for lsp
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Prevent Windows min/max macros from conflicting with C++ std::min/std::max
if(WIN32)
  add_definitions(-DNOMINMAX)
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED True)

# Build type configuration - default to Debug if not specified
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Debug CACHE STRING "Build type" FORCE)
endif()

# Debug flags for better debugging
if(CMAKE_BUILD_TYPE STREQUAL "Debug")
  if(MSVC)
    # MSVC debug flags
    add_compile_options(/Zi /Od /RTC1)
    add_link_options(/DEBUG)
    message(STATUS "Building in Debug mode with MSVC debug symbols")
  else()
    # GCC/Clang debug flags
    add_compile_options(-g3 -O0 -fno-omit-frame-pointer)
    message(STATUS "Building in Debug mode with full debug symbols")
  endif()
endif()

add_subdirectory(common)
add_subdirectory(client)
add_subdirectory(server)
add_subdirectory(tests)
