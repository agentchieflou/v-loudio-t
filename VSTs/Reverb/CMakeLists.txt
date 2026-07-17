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
    FORMATS VST3
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
)

target_compile_definitions(LoudioReverb PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
)

target_link_libraries(LoudioReverb PRIVATE
    juce::juce_audio_utils
    juce::juce_dsp
    juce::juce_gui_basics
PUBLIC
    juce::juce_recommended_config_flags
    juce::juce_recommended_lto_flags
    juce::juce_recommended_warning_flags
)
