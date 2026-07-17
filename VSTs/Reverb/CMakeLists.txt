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
    ../../Framework/src/graphics.c
    ../../Framework/src/font.c
    ../../Framework/src/widget.c
    ../../Framework/src/window_win32.c
    ../../Framework/src/theme.c
    ../../Framework/src/theme_hello_kitty.c
)

target_include_directories(LoudioReverb PRIVATE src ../../Framework/include)

target_compile_definitions(LoudioReverb PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
)

target_link_libraries(LoudioReverb PRIVATE
    juce::juce_audio_utils
    juce::juce_dsp
    juce::juce_gui_basics
    opengl32
PUBLIC
    juce::juce_recommended_config_flags
    juce::juce_recommended_lto_flags
    juce::juce_recommended_warning_flags
)

option(LOUDIO_REVERB_BUILD_TESTS "Build DSP unit tests executable" ON)
if(LOUDIO_REVERB_BUILD_TESTS)
    add_executable(dsp_tests src/DSPTests.cpp)
    target_include_directories(dsp_tests PRIVATE src ../../Framework/include)
endif()
