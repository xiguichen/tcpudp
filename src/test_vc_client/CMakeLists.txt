cmake_minimum_required(VERSION 3.28)

project(TestVCClient)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED True)

# Set the build type to Debug by default if not specified
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Debug)
endif()

# Add source files
file(GLOB SOURCES "*.cpp")

# Add executable
add_executable(test_vc_client ${SOURCES})

# Add common library as dependency
target_link_libraries(test_vc_client common)

# Add common library as include directory
target_include_directories(test_vc_client PRIVATE ${CMAKE_SOURCE_DIR}/common)

set_target_properties(test_vc_client PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

# Enable /EHsc and generate PDB files for Windows platform
if (WIN32)
    target_compile_options(test_vc_client PRIVATE /EHsc /Zi)
    set_target_properties(test_vc_client PROPERTIES COMPILE_PDB_NAME "test_vc_client")
    set_target_properties(test_vc_client PROPERTIES COMPILE_PDB_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/pdb")
    set_target_properties(test_vc_client PROPERTIES LINK_FLAGS "/DEBUG")
endif()
