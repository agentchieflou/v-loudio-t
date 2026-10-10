cmake_minimum_required(VERSION 3.21)
project(LoudioReverb VERSION 0.1.0)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_subdirectory(JUCE)

juce_add_plugin(LoudioReverb
    PRODUCT_NAME "Loudio Reverb"
    COMPANY_NAME "Loudio"
    PLUGIN_MANUFACTURER_CODE Loud
    PLUGIN_CODE Rvb1
    FORMATS VST3 Standalone
    VST3_CATEGORIES Fx Reverb
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS TRUE
)

juce_generate_juce_header(LoudioReverb)

target_sources(LoudioReverb PRIVATE
    src/PluginProcessor.cpp
    src/PluginEditor.cpp
    src/ReverbCore.cpp
    ../../Framework/src/graphics.c
    ../../Framework/src/font.c
    ../../Framework/src/widget.c
    ../../Framework/src/window_win32.c
    ../../Framework/src/theme.c
    ../../Framework/src/theme_hello_kitty.c
    ../../Framework/src/theme_greens.c
)

target_include_directories(LoudioReverb PRIVATE src ../../Framework/include)

target_compile_definitions(LoudioReverb PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
)

target_link_libraries(LoudioReverb PRIVATE
    juce::juce_dsp
    juce::juce_gui_basics
    opengl32
PUBLIC
    juce::juce_recommended_config_flags
    juce::juce_recommended_lto_flags
    juce::juce_recommended_warning_flags
)

# juce_audio_utils (and its transitive juce_audio_devices/juce_audio_formats --
# ASIO/WASAPI backends, FLAC/OggVorbis/AIFF/WAV codec tables) is only needed by
# the Standalone format's AudioDeviceManager/StandaloneFilterWindow glue. The
# VST3 format never touches an audio device or file codec directly -- the host
# owns I/O -- and nothing in src/ references juce_audio_utils at all, so
# linking it into the shared LoudioReverb target inflated the VST3 binary with
# unused code for no reason. Scoped to the Standalone target only (#94).
if(TARGET LoudioReverb_Standalone)
    target_link_libraries(LoudioReverb_Standalone PRIVATE juce::juce_audio_utils)
endif()

option(LOUDIO_REVERB_BUILD_TESTS "Build DSP unit tests executable" ON)
if(LOUDIO_REVERB_BUILD_TESTS)
    add_executable(dsp_tests
        src/DSPTests.cpp
        src/ReverbCore.cpp
        ../../Framework/src/theme.c
        ../../Framework/src/theme_hello_kitty.c
        ../../Framework/src/theme_greens.c
    )
    target_include_directories(dsp_tests PRIVATE src ../../Framework/include)
    # Golden impulse responses for the null test (#44) live next to the
    # sources, not in the build tree, so dsp_tests finds them from any cwd.
    target_compile_definitions(dsp_tests PRIVATE
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
        src/ReverbCore.cpp
        ../../Framework/src/window_win32.c
        ../../Framework/src/graphics.c
        ../../Framework/src/font.c
        ../../Framework/src/widget.c
        ../../Framework/src/theme.c
        ../../Framework/src/theme_hello_kitty.c
        ../../Framework/src/theme_greens.c
    )
    target_include_directories(lpi_reverb PRIVATE src ../../Framework/include ../../PluginABI/include)
    target_compile_definitions(lpi_reverb PRIVATE UNICODE _UNICODE)
    target_link_libraries(lpi_reverb PRIVATE opengl32 gdi32 user32)
    set_target_properties(lpi_reverb PROPERTIES
        OUTPUT_NAME "lpi_reverb"
        PREFIX "" # MinGW would otherwise emit liblpi_reverb.dll; the harness loads lpi_reverb.dll
        RUNTIME_OUTPUT_DIRECTORY "${LPI_REVERB_OUTPUT_DIR}"
    )

    add_executable(lpi_reverb_gui_harness test/lpi_reverb_gui_harness.c)
    target_include_directories(lpi_reverb_gui_harness PRIVATE ../../PluginABI/include)
    add_dependencies(lpi_reverb_gui_harness lpi_reverb)
    set_target_properties(lpi_reverb_gui_harness PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${LPI_REVERB_OUTPUT_DIR}"
    )
endif()
