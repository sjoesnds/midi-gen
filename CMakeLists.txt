cmake_minimum_required(VERSION 3.22)
project(MidiForge VERSION 0.43.0)

include(FetchContent)
FetchContent_Declare(JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG 8.0.14
    GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(JUCE)

juce_add_plugin(MidiForge
    COMPANY_NAME "MidiForge"
    PRODUCT_NAME "MIDI Forge"
    PLUGIN_MANUFACTURER_CODE Mfor
    PLUGIN_CODE Mifo
    FORMATS VST3
    IS_SYNTH TRUE
    IS_MIDI_EFFECT FALSE
    NEEDS_MIDI_INPUT TRUE
    NEEDS_MIDI_OUTPUT TRUE
    COPY_PLUGIN_AFTER_BUILD TRUE
)

target_sources(MidiForge PRIVATE
    Source/PluginProcessor.cpp
    Source/PluginProcessor.h
    Source/PluginEditor.cpp
    Source/PluginEditor.h
)

juce_generate_juce_header(MidiForge)

target_compile_features(MidiForge PRIVATE cxx_std_17)
target_compile_definitions(MidiForge PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_VST3_CAN_REPLACE_VST2=0)

if (MSVC)
    # Исходники содержат кириллические комментарии (UTF-8) — без этого флага
    # MSVC может неверно интерпретировать многобайтовые символы и ломать парсинг
    # кода дальше по файлу (именно это вызывало C2447 на пустом месте).
    target_compile_options(MidiForge PRIVATE /utf-8)
endif()

target_link_libraries(MidiForge PRIVATE
    juce::juce_audio_utils
    juce::juce_audio_processors
    juce::juce_audio_basics
    juce::juce_gui_basics
    juce::juce_recommended_config_flags
    juce::juce_recommended_lto_flags
    juce::juce_recommended_warning_flags
)
