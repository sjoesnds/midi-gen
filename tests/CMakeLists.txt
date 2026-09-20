cmake_minimum_required(VERSION 3.22)
project(MidiForgeQA VERSION 0.44.0)

# Headless quality checks for the generator (no GUI, no plugin format needed).
# Build:  cmake -S tests -B build-qa -DCMAKE_BUILD_TYPE=Release && cmake --build build-qa && ./build-qa/MidiForgeQA_artefacts/Release/MidiForgeQA
include(FetchContent)
FetchContent_Declare(JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG 8.0.14
    GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(JUCE)

juce_add_console_app(MidiForgeQA PRODUCT_NAME "MidiForgeQA")

target_sources(MidiForgeQA PRIVATE
    qa_main.cpp
    ../Source/PluginProcessor.cpp
)
target_include_directories(MidiForgeQA PRIVATE ../Source)
juce_generate_juce_header(MidiForgeQA)

target_compile_features(MidiForgeQA PRIVATE cxx_std_17)
target_compile_definitions(MidiForgeQA PRIVATE
    MIDIFORGE_HEADLESS=1
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
)

target_link_libraries(MidiForgeQA PRIVATE
    juce::juce_audio_processors_headless
    juce::juce_audio_basics
    juce::juce_data_structures
    juce::juce_events
    juce::juce_core
    juce::juce_recommended_config_flags
)
