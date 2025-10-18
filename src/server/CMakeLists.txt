cmake_minimum_required(VERSION 3.28)

# Project name and version
project(Server VERSION 1.0)

# Specify the C++ standard
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED True)

# Add the executable
file(GLOB_RECURSE ALL_CPP_FILES RELATIVE "${CMAKE_CURRENT_SOURCE_DIR}" "*.cpp")

add_executable(server ${ALL_CPP_FILES})

# Add common library as dependency
target_link_libraries(server common)

# Add common library as include directory
target_include_directories(server PRIVATE ${CMAKE_SOURCE_DIR}/common)

set_target_properties(server PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

# Enable /EHsc for Windows platform
if (WIN32)
    target_compile_options(server PRIVATE /EHsc)
endif()
