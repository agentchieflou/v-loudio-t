cmake_minimum_required(VERSION 3.21)
project(lpi C)

set(CMAKE_C_STANDARD 17)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

if(NOT WIN32)
    message(FATAL_ERROR "LPI's example plugin/harness currently only support Windows, matching cuif's own Windows-only stance. The lpi.h header itself has no such restriction. See Plugins/Reverb/ARCHITECTURE_DECISIONS.md.")
endif()

# Header-only ABI -- a plugin implementing lpi.h is not required to use
# anything else in this repo. Deliberately not linked against cuif or any
# other target.
add_library(lpi_abi INTERFACE)
target_include_directories(lpi_abi INTERFACE include)

# All build outputs land in one shared directory so LoadLibraryW's default
# search path (the calling process's own directory) finds the toy plugin
# DLL without needing an absolute path baked into the harness/test sources.
set(LPI_SHARED_OUTPUT_DIR "${CMAKE_BINARY_DIR}/bin/$<CONFIG>")

option(LPI_BUILD_EXAMPLES "Build the LPI toy example plugin(s)" ON)
if(LPI_BUILD_EXAMPLES)
    add_library(lpi_toy_gain SHARED examples/toy_gain/toy_gain.c)
    target_link_libraries(lpi_toy_gain PRIVATE lpi_abi)
    target_include_directories(lpi_toy_gain PRIVATE ../Framework/include)
    set_target_properties(lpi_toy_gain PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${LPI_SHARED_OUTPUT_DIR}"
        OUTPUT_NAME "lpi_toy_gain"
    )
endif()

option(LPI_BUILD_TESTS "Build the LPI host harness and RT-safety stress test" ON)
if(LPI_BUILD_TESTS AND LPI_BUILD_EXAMPLES)
    add_executable(lpi_host_harness test/host_harness/host_harness.c)
    target_link_libraries(lpi_host_harness PRIVATE lpi_abi)
    add_dependencies(lpi_host_harness lpi_toy_gain)
    set_target_properties(lpi_host_harness PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${LPI_SHARED_OUTPUT_DIR}"
    )

    add_executable(lpi_rt_safety_test test/rt_safety/rt_safety_test.c)
    target_link_libraries(lpi_rt_safety_test PRIVATE lpi_abi)
    add_dependencies(lpi_rt_safety_test lpi_toy_gain)
    set_target_properties(lpi_rt_safety_test PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${LPI_SHARED_OUTPUT_DIR}"
    )
endif()
