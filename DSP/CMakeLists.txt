cmake_minimum_required(VERSION 3.21)
project(LoudioDsp CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Header-only, JUCE-free, platform-free DSP building blocks shared by every
# plugin: delay lines, all-pass diffusion, early reflections, one-pole and
# biquad filters, a 3-band crossover, an envelope detector, a radix-2 FFT
# with a log-spaced analyzer, and a linear parameter smoother.
add_library(loudio_dsp INTERFACE)
target_include_directories(loudio_dsp INTERFACE include)

option(LOUDIO_DSP_BUILD_TESTS "Build the DSP component unit tests" ON)
if(LOUDIO_DSP_BUILD_TESTS)
    add_executable(loudio_dsp_tests test/dsp_tests.cpp)
    target_link_libraries(loudio_dsp_tests PRIVATE loudio_dsp)
endif()
