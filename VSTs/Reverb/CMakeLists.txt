cmake_minimum_required(VERSION 3.25)
project(LoudioReverb VERSION 0.1.0)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Shared DSP building blocks (DSP/include/loudio/dsp), header-only.
set(LOUDIO_DSP_INCLUDE ${CMAKE_CURRENT_SOURCE_DIR}/../../DSP/include)

# ON: build only dsp_tests -- no VST3 SDK, no plugin targets. This is what
# the Linux CI job uses for fast DSP feedback. LOUDIO_PLUGIN_TESTS_ONLY is
# the name every plugin shares (the CI matrix passes it);
# LOUDIO_REVERB_TESTS_ONLY is kept as an alias.
option(LOUDIO_PLUGIN_TESTS_ONLY "Build only the DSP tests (skips the VST3 SDK and all plugin targets)" OFF)
option(LOUDIO_REVERB_TESTS_ONLY "Alias of LOUDIO_PLUGIN_TESTS_ONLY" OFF)
if(LOUDIO_REVERB_TESTS_ONLY)
    set(LOUDIO_PLUGIN_TESTS_ONLY ON)
endif()

if(NOT LOUDIO_PLUGIN_TESTS_ONLY)
# VST3 on the VST3 SDK directly (#171), no JUCE: the shared shell in
# VSTs/common/vst3 plus vst3_reverb.cpp. Builds on Windows, macOS and
# Linux; the cuif editor (ReverbUi) is Windows-only, like cuif itself.
# No Standalone: see VSTs/Reverb/README.md.
include(${CMAKE_CURRENT_SOURCE_DIR}/../common/vst3/LoudioVst3.cmake)

loudio_add_vst3_plugin(LoudioReverb_VST3
    PACKAGE_NAME "Loudio Reverb"
    SOURCES
        src/vst3_reverb.cpp
        src/ReverbCore.cpp
    EDITOR_SOURCES
        src/ReverbUi.cpp
)

# Same sound (#171): the built bundle, hosted through the SDK's hosting
# classes, against ReverbCore run directly and (Windows) lpi_reverb.dll.
add_executable(vst3_reverb_tests
    test/vst3_reverb_tests.cpp
    src/ReverbCore.cpp
    ${LOUDIO_VST3SDK_DIR}/public.sdk/source/common/memorystream.cpp
    ${LOUDIO_VST3_HOSTING_MODULE_SOURCE}
)
target_link_libraries(vst3_reverb_tests PRIVATE sdk_hosting ${LOUDIO_VST3_HOSTING_LIBS})
target_include_directories(vst3_reverb_tests PRIVATE src ../common ../../Framework/include ${LOUDIO_DSP_INCLUDE} ../../PluginABI/include)
target_compile_features(vst3_reverb_tests PRIVATE cxx_std_20)
get_target_property(LOUDIO_REVERB_VST3_BUNDLE LoudioReverb_VST3 LOUDIO_VST3_BUNDLE)
target_compile_definitions(vst3_reverb_tests PRIVATE
    NOMINMAX
    LOUDIO_REVERB_VST3_BUNDLE="${LOUDIO_REVERB_VST3_BUNDLE}"
    LOUDIO_PLUGIN_VERSION="${PROJECT_VERSION}"
)
add_dependencies(vst3_reverb_tests LoudioReverb_VST3)
endif() # NOT LOUDIO_PLUGIN_TESTS_ONLY

option(LOUDIO_REVERB_BUILD_TESTS "Build DSP unit tests executable" ON)
if(LOUDIO_REVERB_BUILD_TESTS)
    add_executable(dsp_tests
        src/DSPTests.cpp
        src/ReverbCore.cpp
        ../../Framework/src/theme.c
        ../../Framework/src/theme_hello_kitty.c
        ../../Framework/src/theme_greens.c
    )
    target_include_directories(dsp_tests PRIVATE src ../../Framework/include ${LOUDIO_DSP_INCLUDE})
    # Golden impulse responses for the null test (#44) live next to the
    # sources, not in the build tree, so dsp_tests finds them from any cwd.
    # NOMINMAX: cuif/ring_buffer.h pulls in <windows.h>, whose min/max
    # macros otherwise break every std::min/std::max in the tests.
    target_compile_definitions(dsp_tests PRIVATE
        NOMINMAX
        LOUDIO_REVERB_GOLDEN_DIR="${CMAKE_CURRENT_SOURCE_DIR}/test/golden"
    )
endif()

# LPI (Loudio Plugin Interface) build of the Reverb (#101/B4): no JUCE, built
# directly against ReverbCore, with an offscreen cuif GUI (lpi.gui.offscreen.v1).
option(LOUDIO_REVERB_BUILD_LPI "Build the LPI-ABI Reverb DLL and its offscreen GUI test harness" ON)
if(LOUDIO_REVERB_BUILD_LPI AND WIN32)
    set(LPI_REVERB_OUTPUT_DIR "${CMAKE_BINARY_DIR}/bin/$<CONFIG>")

    add_library(lpi_reverb SHARED
        src/lpi_reverb.cpp
        src/ReverbUi.cpp
        src/ReverbCore.cpp
        ../../Framework/src/window_win32.c
        ../../Framework/src/graphics.c
        ../../Framework/src/font.c
        ../../Framework/src/widget.c
        ../../Framework/src/theme.c
        ../../Framework/src/theme_hello_kitty.c
        ../../Framework/src/theme_greens.c
    )
    target_include_directories(lpi_reverb PRIVATE src ../../Framework/include ../../PluginABI/include ../../PluginABI/wrapper ${LOUDIO_DSP_INCLUDE})
    target_compile_definitions(lpi_reverb PRIVATE UNICODE _UNICODE NOMINMAX)
    target_link_libraries(lpi_reverb PRIVATE opengl32 gdi32 user32)
    set_target_properties(lpi_reverb PROPERTIES
        OUTPUT_NAME "lpi_reverb"
        PREFIX "" # MinGW would otherwise emit liblpi_reverb.dll; the harness loads lpi_reverb.dll
        RUNTIME_OUTPUT_DIRECTORY "${LPI_REVERB_OUTPUT_DIR}"
    )

    # PluginABI's generic conformance suite, run here against lpi_reverb.dll.
    add_executable(lpi_conformance ../../PluginABI/test/conformance/lpi_conformance.c)
    target_include_directories(lpi_conformance PRIVATE ../../PluginABI/include)
    add_dependencies(lpi_conformance lpi_reverb)
    set_target_properties(lpi_conformance PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${LPI_REVERB_OUTPUT_DIR}"
    )

    add_executable(lpi_reverb_gui_harness test/lpi_reverb_gui_harness.c)
    target_include_directories(lpi_reverb_gui_harness PRIVATE ../../PluginABI/include)
    add_dependencies(lpi_reverb_gui_harness lpi_reverb)
    set_target_properties(lpi_reverb_gui_harness PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${LPI_REVERB_OUTPUT_DIR}"
    )
endif()
