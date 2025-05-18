# Specify the minimum version for CMake
cmake_minimum_required(VERSION 3.16)

# Project's name
project(CommonTest)

include(FetchContent)
FetchContent_Declare(
  googletest
  # Specify the commit you depend on and update it regularly.
  URL https://github.com/google/googletest/archive/5376968f6948923e2411081fd9372e71a59d8e77.zip
)
# For Windows: Prevent overriding the parent project's compiler/linker settings
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

# Specify the C++ standard
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED True)

# add a macro to disable secure warnings
add_definitions(-D_CRT_SECURE_NO_WARNINGS)

# Prevent Windows min/max macros from conflicting with C++ std::min/std::max
if(WIN32)
  add_definitions(-DNOMINMAX)
endif()

# Include directories
include_directories("${CMAKE_SOURCE_DIR}/third_party")

# add google test include directories
include_directories(${googletest_SOURCE_DIR}/googletest/include)

# include common directory
include_directories(${CMAKE_SOURCE_DIR}/common)

# include server directory
include_directories(${CMAKE_SOURCE_DIR}/server)

# include client directory
include_directories(${CMAKE_SOURCE_DIR}/client)

# link google test libraries
link_directories(${googletest_BINARY_DIR})

# find all source files in the current directory
file(GLOB_RECURSE SOURCES "*.cpp" "*.h")

# find all source files in the server directory
file(GLOB_RECURSE SERVER_SOURCES "${CMAKE_SOURCE_DIR}/server/*.cpp" "${CMAKE_SOURCE_DIR}/server/*.h")

# We'll only include the test files, not the actual client implementation
# to avoid dependency issues

# Define the executable
add_executable(CommonTest
    ${SOURCES}
    ${SERVER_SOURCES}
)

# Link the common library
target_link_libraries(CommonTest PRIVATE
    common
    gtest_main
)

# Header files
target_include_directories(CommonTest PRIVATE
    ${CMAKE_SOURCE_DIR}
)

# Link Windows Socket library
if(WIN32)
    target_link_libraries(CommonTest PRIVATE
        ws2_32
    )
endif()

# Precompiled header (pch.h)
# target_precompile_headers(CommonTest PRIVATE pch.h)

# Set properties for different configurations
set_target_properties(CommonTest PROPERTIES
    COMPILE_PDB_NAME "CommonTest"
    COMPILE_PDB_FILE "CommonTest.pdb"
)


target_compile_definitions(CommonTest PRIVATE EXCLUDE_MAIN)

add_test(NAME CommonTest COMMAND CommonTest) 

