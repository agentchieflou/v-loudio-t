cmake_minimum_required(VERSION 3.21)
project(LoudioCompressor VERSION 0.1.0)

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

# ON: build only dsp_tests -- no JUCE, no plugin targets (the Linux CI job).
option(LOUDIO_PLUGIN_TESTS_ONLY "Build only the DSP tests (skips JUCE and all plugin targets)" OFF)

if(NOT LOUDIO_PLUGIN_TESTS_ONLY)
# The JUCE submodule is shared by every plugin: VSTs/JUCE.
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../JUCE ${CMAKE_BINARY_DIR}/juce)

juce_add_plugin(LoudioCompressor
    PRODUCT_NAME "Loudio Compressor"
    COMPANY_NAME "Loudio"
    PLUGIN_MANUFACTURER_CODE Loud
    PLUGIN_CODE Com1
    FORMATS VST3 Standalone
    VST3_CATEGORIES Fx
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS TRUE
)

juce_generate_juce_header(LoudioCompressor)

target_sources(LoudioCompressor PRIVATE
    src/PluginProcessor.cpp
    src/CompressorUi.cpp
    src/CompressorCore.cpp
    ${LOUDIO_CUIF_SOURCES}
)

target_include_directories(LoudioCompressor PRIVATE src ../common ../../Framework/include ${LOUDIO_DSP_INCLUDE})

target_compile_definitions(LoudioCompressor PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
)

target_link_libraries(LoudioCompressor PRIVATE
    juce::juce_dsp
    juce::juce_gui_basics
    opengl32
PUBLIC
    juce::juce_recommended_config_flags
    juce::juce_recommended_lto_flags
    juce::juce_recommended_warning_flags
)

# juce_audio_utils only for the Standalone's device/file glue (see Reverb, #94).
if(TARGET LoudioCompressor_Standalone)
    target_link_libraries(LoudioCompressor_Standalone PRIVATE juce::juce_audio_utils)
endif()
endif() # NOT LOUDIO_PLUGIN_TESTS_ONLY

option(LOUDIO_COMPRESSOR_BUILD_TESTS "Build DSP unit tests executable" ON)
if(LOUDIO_COMPRESSOR_BUILD_TESTS)
    add_executable(dsp_tests
        src/DSPTests.cpp
        src/CompressorCore.cpp
    )
    target_include_directories(dsp_tests PRIVATE src ../../Framework/include ${LOUDIO_DSP_INCLUDE})
    # NOMINMAX: cuif/ring_buffer.h pulls in <windows.h>, whose min/max
    # macros otherwise break std::min/std::max.
    target_compile_definitions(dsp_tests PRIVATE
        NOMINMAX
        LOUDIO_COMPRESSOR_GOLDEN_DIR="${CMAKE_CURRENT_SOURCE_DIR}/test/golden"
    )
endif()

# LPI (Loudio Plugin Interface) build: no JUCE, on PluginABI/wrapper's template.
option(LOUDIO_COMPRESSOR_BUILD_LPI "Build the LPI-ABI DLL and its offscreen GUI test harness" ON)
if(LOUDIO_COMPRESSOR_BUILD_LPI AND WIN32)
    set(LPI_OUTPUT_DIR "${CMAKE_BINARY_DIR}/bin/$<CONFIG>")

    add_library(lpi_compressor SHARED
        src/lpi_compressor.cpp
        src/CompressorUi.cpp
        src/CompressorCore.cpp
        ${LOUDIO_CUIF_SOURCES}
    )
    target_include_directories(lpi_compressor PRIVATE src ../../Framework/include ../../PluginABI/include ../../PluginABI/wrapper ${LOUDIO_DSP_INCLUDE})
    target_compile_definitions(lpi_compressor PRIVATE UNICODE _UNICODE NOMINMAX)
    target_link_libraries(lpi_compressor PRIVATE opengl32 gdi32 user32)
    set_target_properties(lpi_compressor PROPERTIES
        OUTPUT_NAME "lpi_compressor"
        PREFIX "" # MinGW would otherwise emit liblpi_compressor.dll; the harness loads lpi_compressor.dll
        RUNTIME_OUTPUT_DIRECTORY "${LPI_OUTPUT_DIR}"
    )

    # PluginABI's generic conformance suite, run here against lpi_compressor.dll.
    add_executable(lpi_conformance ../../PluginABI/test/conformance/lpi_conformance.c)
    target_include_directories(lpi_conformance PRIVATE ../../PluginABI/include)
    add_dependencies(lpi_conformance lpi_compressor)
    set_target_properties(lpi_conformance PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${LPI_OUTPUT_DIR}")

    add_executable(lpi_compressor_gui_harness test/lpi_compressor_gui_harness.c)
    target_include_directories(lpi_compressor_gui_harness PRIVATE ../../PluginABI/include)
    add_dependencies(lpi_compressor_gui_harness lpi_compressor)
    set_target_properties(lpi_compressor_gui_harness PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${LPI_OUTPUT_DIR}")
endif()
