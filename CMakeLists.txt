cmake_minimum_required(VERSION 3.15)
project(MAJED_AUDIO_CHORD_EXTRACTOR VERSION 1.0.0)

set(CMAKE_CXX_STANDARD 17)

add_subdirectory(JUCE)

juce_add_plugin(MajedAudioChordExtractor
    COMPANY_NAME "Majed Audio"
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    COPY_PLUGIN_AFTER_BUILD FALSE
    PLUGIN_MANUFACTURER_CODE Majd
    PLUGIN_CODE Mjce
    FORMATS VST3
    PRODUCT_NAME "MAJED AUDIO VST3"
)

target_sources(MajedAudioChordExtractor
    PRIVATE
        PluginProcessor.cpp
        PluginEditor.cpp
)

target_compile_definitions(MajedAudioChordExtractor
    PUBLIC
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0
)

target_link_libraries(MajedAudioChordExtractor
    PRIVATE
        juce::juce_audio_utils
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags
)
