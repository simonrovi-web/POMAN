cmake_minimum_required(VERSION 3.22)

project(AbyssalAmp VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Static C++ runtime so Abyssal.exe runs on machines without the VC++ redistributable.
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

# JUCE lives in external/JUCE (git clone --depth 1 --branch 8.0.15 https://github.com/juce-framework/JUCE.git external/JUCE)
add_subdirectory(external/JUCE)

juce_add_plugin(AbyssalAmp
    COMPANY_NAME "Abyssal Audio"
    COMPANY_COPYRIGHT "Copyright (c) 2026 Abyssal Audio"
    DESCRIPTION "Abyssal guitar amplifier"
    PRODUCT_NAME "Abyssal"
    ICON_BIG Resources/icon.png
    BUNDLE_ID "com.abyssalaudio.abyssal"
    PLUGIN_MANUFACTURER_CODE Abys
    PLUGIN_CODE Aby1
    FORMATS VST3 Standalone
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD FALSE
    VST3_CATEGORIES Fx Distortion)

target_sources(AbyssalAmp PRIVATE
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp
    Source/StandaloneApp.cpp)

target_include_directories(AbyssalAmp PRIVATE Source)

target_compile_definitions(AbyssalAmp PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0
    JUCE_USE_CUSTOM_PLUGIN_STANDALONE_APP=1)

target_link_libraries(AbyssalAmp
    PRIVATE
        juce::juce_audio_utils
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)
