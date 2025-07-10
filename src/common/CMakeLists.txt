cmake_minimum_required(VERSION 3.28)

# Project name and version
project(common VERSION 1.0)

find_package(Protobuf REQUIRED)

# Specify the C++ standard
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED True)

# Add the executable
file(GLOB_RECURSE ALL_CPP_FILES RELATIVE "${CMAKE_CURRENT_SOURCE_DIR}" "*.cpp")

# Create a library
add_library(common STATIC ${ALL_CPP_FILES})

target_include_directories(common PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/../proto)
target_link_libraries(common proto)

# Enable /EHsc for Windows platform
if (WIN32)
    target_compile_options(common PRIVATE /EHsc)
endif()
