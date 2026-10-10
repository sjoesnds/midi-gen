cmake_minimum_required(VERSION 3.22)
project(MidiForgeQA VERSION 0.105.2)

# Headless release gates for the current melody-only product (no GUI or plugin host).
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
    qa_melody_only.cpp
    ../Source/PluginProcessor.cpp
    ../Source/MidiForgeShared.h
    ../Source/MidiForgeMidiIO.cpp
    ../Source/MidiForgeState.cpp
    ../Source/MidiForgeTaste.cpp
    ../Source/RhythmGrammar.cpp
    ../Source/ComposerGrammar.cpp
    ../Source/MelodicProsody.cpp
    ../Source/CreativeRange.cpp
    ../Source/MelodyIntent.cpp
    ../Source/MelodyDecision.cpp
    ../Source/MelodyDecision.h
    ../Source/MotifSemantics.cpp
    ../Source/LoopClosure.cpp
)
target_include_directories(MidiForgeQA PRIVATE ../Source)
juce_generate_juce_header(MidiForgeQA)

target_compile_features(MidiForgeQA PRIVATE cxx_std_17)
target_compile_definitions(MidiForgeQA PRIVATE
    MIDIFORGE_HEADLESS=1
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    MIDIFORGE_ENGINE_VERSION="${PROJECT_VERSION}"
)

target_link_libraries(MidiForgeQA PRIVATE
    juce::juce_audio_processors_headless
    juce::juce_audio_basics
    juce::juce_data_structures
    juce::juce_events
    juce::juce_core
    juce::juce_recommended_config_flags
)
