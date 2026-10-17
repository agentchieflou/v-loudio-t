cmake_minimum_required(VERSION 3.25)
project(LoudioDelay VERSION 0.1.0)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Shared DSP building blocks + plugin platform (DSP/include/loudio), header-only.
set(LOUDIO_DSP_INCLUDE ${CMAKE_CURRENT_SOURCE_DIR}/../../DSP/include)
set(LOUDIO_FRAMEWORK_SRC ${CMAKE_CURRENT_SOURCE_DIR}/../../Framework/src)
set(LOUDIO_CUIF_SOURCES
    ${LOUDIO_FRAMEWORK_SRC}/graphics.c
    ${LOUDIO_FRAMEWORK_SRC}/font.c
    ${LOUDIO_FRAMEWORK_SRC}/widget.c
    ${LOUDIO_FRAMEWORK_SRC}/window_win32.c
    ${LOUDIO_FRAMEWORK_SRC}/theme.c
    ${LOUDIO_FRAMEWORK_SRC}/theme_hello_kitty.c
    ${LOUDIO_FRAMEWORK_SRC}/theme_greens.c
)

# ON: build only dsp_tests -- no VST3 SDK, no plugin targets (the Linux CI job).
option(LOUDIO_PLUGIN_TESTS_ONLY "Build only the DSP tests (skips the VST3 SDK and all plugin targets)" OFF)

if(NOT LOUDIO_PLUGIN_TESTS_ONLY)
# VST3 on the VST3 SDK directly (#171, VSTs/common/vst3), on Windows, macOS
# and Linux; the cuif editor is Windows-only, like cuif itself.
include(${CMAKE_CURRENT_SOURCE_DIR}/../common/vst3/LoudioVst3.cmake)

loudio_add_vst3_plugin(LoudioDelay_VST3
    PACKAGE_NAME "Loudio Delay"
    SOURCES
        src/vst3_delay.cpp
        src/DelayCore.cpp
    EDITOR_SOURCES
        src/DelayUi.cpp
)
loudio_add_vst3_tests(vst3_delay_tests LoudioDelay_VST3
    test/vst3_delay_tests.cpp
    src/DelayCore.cpp
)
endif() # NOT LOUDIO_PLUGIN_TESTS_ONLY

option(LOUDIO_DELAY_BUILD_TESTS "Build DSP unit tests executable" ON)
if(LOUDIO_DELAY_BUILD_TESTS)
    add_executable(dsp_tests
        src/DSPTests.cpp
        src/DelayCore.cpp
    )
    target_include_directories(dsp_tests PRIVATE src ../../Framework/include ${LOUDIO_DSP_INCLUDE})
    # NOMINMAX: cuif/ring_buffer.h pulls in <windows.h>, whose min/max
    # macros otherwise break std::min/std::max.
    target_compile_definitions(dsp_tests PRIVATE
        NOMINMAX
        LOUDIO_DELAY_GOLDEN_DIR="${CMAKE_CURRENT_SOURCE_DIR}/test/golden"
    )
endif()

# LPI (Loudio Plugin Interface) build: no JUCE, on PluginABI/wrapper's template.
option(LOUDIO_DELAY_BUILD_LPI "Build the LPI-ABI DLL and its offscreen GUI test harness" ON)
if(LOUDIO_DELAY_BUILD_LPI AND WIN32)
    set(LPI_OUTPUT_DIR "${CMAKE_BINARY_DIR}/bin/$<CONFIG>")

    add_library(lpi_delay SHARED
        src/lpi_delay.cpp
        src/DelayUi.cpp
        src/DelayCore.cpp
        ${LOUDIO_CUIF_SOURCES}
    )
    target_include_directories(lpi_delay PRIVATE src ../../Framework/include ../../PluginABI/include ../../PluginABI/wrapper ${LOUDIO_DSP_INCLUDE})
    target_compile_definitions(lpi_delay PRIVATE UNICODE _UNICODE NOMINMAX)
    target_link_libraries(lpi_delay PRIVATE opengl32 gdi32 user32)
    set_target_properties(lpi_delay PROPERTIES
        OUTPUT_NAME "lpi_delay"
        PREFIX "" # MinGW would otherwise emit liblpi_delay.dll; the harness loads lpi_delay.dll
        RUNTIME_OUTPUT_DIRECTORY "${LPI_OUTPUT_DIR}"
    )

    # PluginABI's generic conformance suite, run here against lpi_delay.dll.
    add_executable(lpi_conformance ../../PluginABI/test/conformance/lpi_conformance.c)
    target_include_directories(lpi_conformance PRIVATE ../../PluginABI/include)
    add_dependencies(lpi_conformance lpi_delay)
    set_target_properties(lpi_conformance PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${LPI_OUTPUT_DIR}")

    add_executable(lpi_delay_gui_harness test/lpi_delay_gui_harness.c)
    target_include_directories(lpi_delay_gui_harness PRIVATE ../../PluginABI/include)
    add_dependencies(lpi_delay_gui_harness lpi_delay)
    set_target_properties(lpi_delay_gui_harness PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${LPI_OUTPUT_DIR}")
endif()
