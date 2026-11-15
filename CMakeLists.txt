cmake_minimum_required(VERSION 3.22)
project(MAJED_AUDIO_ARABIC_CHORD_EXTRACTOR VERSION 0.1.0)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# JUCE is fetched automatically by CMake on the build machine.
include(FetchContent)
FetchContent_Declare(
    JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG 8.0.6
)
FetchContent_MakeAvailable(JUCE)

juce_add_plugin(MAJED_AUDIO_ARABIC_CHORD_EXTRACTOR
    COMPANY_NAME "MAJED AUDIO"
    PRODUCT_NAME "MAJED AUDIO Arabic Chord Extractor"
    PLUGIN_MANUFACTURER_CODE MjAu
    PLUGIN_CODE Mj01
    FORMATS VST3
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT TRUE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS TRUE
    COPY_PLUGIN_AFTER_BUILD FALSE
)

target_sources(MAJED_AUDIO_ARABIC_CHORD_EXTRACTOR PRIVATE
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp
)

target_compile_definitions(MAJED_AUDIO_ARABIC_CHORD_EXTRACTOR PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
)

target_link_libraries(MAJED_AUDIO_ARABIC_CHORD_EXTRACTOR PRIVATE
    juce::juce_audio_utils
    juce::juce_dsp
    juce::juce_gui_extra
    PUBLIC
    juce::juce_recommended_config_flags
    juce::juce_recommended_lto_flags
    juce::juce_recommended_warning_flags
)
