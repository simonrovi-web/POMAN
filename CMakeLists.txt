cmake_minimum_required(VERSION 3.22)

project(POMAN VERSION 0.3.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Static C++ runtime so POMAN.exe runs on machines without the VC++ redistributable.
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

# JUCE lives in external/JUCE (git clone --depth 1 --branch 8.0.15 https://github.com/juce-framework/JUCE.git external/JUCE)
add_subdirectory(external/JUCE)

juce_add_plugin(POMAN
    COMPANY_NAME "POMAN Audio"
    COMPANY_COPYRIGHT "Copyright (c) 2026 POMAN Audio"
    DESCRIPTION "POMAN guitar amp suite"
    PRODUCT_NAME "POMAN"
    ICON_BIG Resources/icon.png
    BUNDLE_ID "com.pomanaudio.poman"
    PLUGIN_MANUFACTURER_CODE Pman
    PLUGIN_CODE Pmn1
    FORMATS VST3 Standalone
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD FALSE
    VST3_CATEGORIES Fx Distortion)

target_sources(POMAN PRIVATE
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp
    Source/StandaloneApp.cpp
    Source/PresetManager.cpp)

target_include_directories(POMAN PRIVATE Source)

target_compile_definitions(POMAN PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0
    JUCE_USE_CUSTOM_PLUGIN_STANDALONE_APP=1
    JUCE_ASIO=1
    JUCE_WASAPI=1)

target_link_libraries(POMAN
    PRIVATE
        juce::juce_audio_utils
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)

# Offline DSP checks: cmake --build build --config Release --target POMANTests
juce_add_console_app(POMANTests PRODUCT_NAME "POMANTests")
target_sources(POMANTests PRIVATE Tests/TunerTest.cpp)
target_include_directories(POMANTests PRIVATE Source)
target_compile_definitions(POMANTests PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)
target_link_libraries(POMANTests PRIVATE juce::juce_audio_basics PUBLIC juce::juce_recommended_config_flags)

juce_add_console_app(POMANDspTests PRODUCT_NAME "POMANDspTests")
target_sources(POMANDspTests PRIVATE Tests/DspTest.cpp)
target_include_directories(POMANDspTests PRIVATE Source)
target_compile_definitions(POMANDspTests PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)
target_link_libraries(POMANDspTests PRIVATE juce::juce_dsp PUBLIC juce::juce_recommended_config_flags)
