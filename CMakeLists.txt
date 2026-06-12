cmake_minimum_required(VERSION 3.22)
project(Obsidian VERSION 0.1.0)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Fetch JUCE automatically (requires internet on first configure)
include(FetchContent)
FetchContent_Declare(
    JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG 8.0.4
    GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(JUCE)

juce_add_plugin(Obsidian
    COMPANY_NAME "ObsidianAudio"
    PLUGIN_MANUFACTURER_CODE Obsn
    PLUGIN_CODE Obs1
    FORMATS VST3 Standalone
    IS_SYNTH TRUE
    NEEDS_MIDI_INPUT TRUE
    NEEDS_MIDI_OUTPUT FALSE
    COPY_PLUGIN_AFTER_BUILD TRUE
    PRODUCT_NAME "Obsidian"
)

juce_generate_juce_header(Obsidian)

target_sources(Obsidian PRIVATE
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp
    Source/BundledPresets.h
)

target_compile_definitions(Obsidian PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
)

target_link_libraries(Obsidian
    PRIVATE
        juce::juce_audio_utils
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags
)
