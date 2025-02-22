cmake_minimum_required(VERSION 3.10)

# Project name and version
project(Server VERSION 1.0)

# Specify the C++ standard
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED True)

# Add the executable
file(GLOB_RECURSE ALL_CPP_FILES RELATIVE "${CMAKE_CURRENT_SOURCE_DIR}" "*.cpp")
add_executable(server ${ALL_CPP_FILES})

