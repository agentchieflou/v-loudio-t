cmake_minimum_required(VERSION 3.21)
project(LoudioDsp CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Header-only, JUCE-free, platform-free DSP building blocks shared by every
# plugin: delay lines, all-pass diffusion, early reflections, one-pole and
# biquad filters, a 3-band crossover, an envelope detector, a radix-2 FFT
# with a log-spaced analyzer, and a linear parameter smoother.
# loudio/plugin (#127) adds the plugin-agnostic parameter table, id-keyed
# state blob and RT parameter queue; the queue is built on the SPSC ring
# buffer from Framework/include/cuif/ring_buffer.h, hence the second include
# directory. On Windows that header pulls in <windows.h>, so consumers
# define NOMINMAX (as every plugin target already does).
add_library(loudio_dsp INTERFACE)
target_include_directories(loudio_dsp INTERFACE include ${CMAKE_CURRENT_SOURCE_DIR}/../Framework/include)
target_compile_definitions(loudio_dsp INTERFACE NOMINMAX)

option(LOUDIO_DSP_BUILD_TESTS "Build the DSP component unit tests" ON)
if(LOUDIO_DSP_BUILD_TESTS)
    add_executable(loudio_dsp_tests test/dsp_tests.cpp)
    target_link_libraries(loudio_dsp_tests PRIVATE loudio_dsp)
endif()
