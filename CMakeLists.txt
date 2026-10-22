cmake_minimum_required(VERSION 3.22)

project(POMAN VERSION 0.16.0 LANGUAGES C CXX)

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
    ICON_BIG Resources/Branding/logo_badge.png
    BUNDLE_ID "com.pomanaudio.poman"
    PLUGIN_MANUFACTURER_CODE Pman
    PLUGIN_CODE Pmn1
    FORMATS VST3 Standalone
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT TRUE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD FALSE
    VST3_CATEGORIES Fx Distortion)

target_sources(POMAN PRIVATE
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp
    Source/StandaloneApp.cpp
    Source/PresetManager.cpp
    Source/recorder/MultitrackRecorder.cpp
    Source/StudioView.cpp
    Source/DrumView.cpp
    Source/LoopView.cpp
    Source/LiveView.cpp
    Source/JamView.cpp
    Source/dsp/NamAmp.cpp)

target_include_directories(POMAN PRIVATE Source)

# Branding images (logo banner for the header, badge for the LIVE page and the app icon).
juce_add_binary_data(POMANAssets SOURCES
    Resources/Branding/logo_banner.png
    Resources/Branding/logo_badge.png)

target_compile_definitions(POMAN PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0
    JUCE_USE_CUSTOM_PLUGIN_STANDALONE_APP=1
    JUCE_ASIO=1
    JUCE_WASAPI=1
    JUCE_USE_MP3AUDIOFORMAT=1)

target_link_libraries(POMAN
    PRIVATE
        nam_core
        POMANAssets
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
target_compile_definitions(POMANDspTests PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 POMAN_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(POMANDspTests PRIVATE juce::juce_dsp juce::juce_data_structures PUBLIC juce::juce_recommended_config_flags)

juce_add_console_app(POMANRecorderTests PRODUCT_NAME "POMANRecorderTests")
target_sources(POMANRecorderTests PRIVATE Tests/RecorderTest.cpp Source/recorder/MultitrackRecorder.cpp)
target_include_directories(POMANRecorderTests PRIVATE Source)
target_compile_definitions(POMANRecorderTests PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)
target_link_libraries(POMANRecorderTests PRIVATE juce::juce_audio_formats juce::juce_events PUBLIC juce::juce_recommended_config_flags)

# Drum voice analysis + WAV previews: POMANDrumAnalysis <output folder>
juce_add_console_app(POMANDrumAnalysis PRODUCT_NAME "POMANDrumAnalysis")
target_sources(POMANDrumAnalysis PRIVATE Tests/DrumAnalysis.cpp)
target_include_directories(POMANDrumAnalysis PRIVATE Source)
target_compile_definitions(POMANDrumAnalysis PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)
target_link_libraries(POMANDrumAnalysis PRIVATE juce::juce_dsp juce::juce_audio_formats juce::juce_data_structures PUBLIC juce::juce_recommended_config_flags)

# CPU and aliasing measurements per DSP module
juce_add_console_app(POMANBench PRODUCT_NAME "POMANBench")
target_sources(POMANBench PRIVATE Tests/Bench.cpp)
target_include_directories(POMANBench PRIVATE Source)
target_compile_definitions(POMANBench PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)
target_link_libraries(POMANBench PRIVATE juce::juce_dsp juce::juce_data_structures PUBLIC juce::juce_recommended_config_flags juce::juce_recommended_lto_flags)

# Neural Amp Modeler core (MIT): external/NeuralAmpModelerCore, tag v0.6.0 with submodules (Eigen, nlohmann/json).
set(NAM_CORE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/external/NeuralAmpModelerCore")
file(GLOB NAM_CORE_SOURCES "${NAM_CORE_DIR}/NAM/*.cpp" "${NAM_CORE_DIR}/NAM/*/*.cpp")
# OBJECT, not STATIC: each architecture registers itself from a static initializer, and a static
# library would let the linker drop those (unreferenced) object files.
add_library(nam_core OBJECT ${NAM_CORE_SOURCES})
target_include_directories(nam_core PUBLIC "${NAM_CORE_DIR}" "${NAM_CORE_DIR}/NAM")
target_include_directories(nam_core SYSTEM PUBLIC "${NAM_CORE_DIR}/Dependencies/eigen" "${NAM_CORE_DIR}/Dependencies/nlohmann")
# float samples end to end; NOMINMAX keeps windows.h macros away from Eigen.
target_compile_definitions(nam_core PUBLIC NAM_SAMPLE_FLOAT NAM_ENABLE_A2_FAST NOMINMAX)
if (MSVC)
    target_compile_options(nam_core PRIVATE /bigobj /W0)
endif()

juce_add_console_app(POMANNamTests PRODUCT_NAME "POMANNamTests")
target_sources(POMANNamTests PRIVATE Tests/NamTest.cpp Source/dsp/NamAmp.cpp)
target_include_directories(POMANNamTests PRIVATE Source)
target_compile_definitions(POMANNamTests PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)
target_link_libraries(POMANNamTests PRIVATE nam_core juce::juce_dsp PUBLIC juce::juce_recommended_config_flags juce::juce_recommended_lto_flags)

# Generates the bundled (synthetic) cabinet IRs: POMANIrGen portable/POMAN-Portable/IRs
juce_add_console_app(POMANIrGen PRODUCT_NAME "POMANIrGen")
target_sources(POMANIrGen PRIVATE Tools/IrGen.cpp)
target_include_directories(POMANIrGen PRIVATE Source)
target_compile_definitions(POMANIrGen PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)
target_link_libraries(POMANIrGen PRIVATE juce::juce_dsp juce::juce_audio_formats PUBLIC juce::juce_recommended_config_flags)

juce_add_console_app(POMANMidiTests PRODUCT_NAME "POMANMidiTests")
target_sources(POMANMidiTests PRIVATE Tests/MidiTest.cpp)
target_include_directories(POMANMidiTests PRIVATE Source)
target_compile_definitions(POMANMidiTests PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)
target_link_libraries(POMANMidiTests PRIVATE juce::juce_audio_basics juce::juce_data_structures PUBLIC juce::juce_recommended_config_flags)

# Dynamics/feel measurements per amp model: POMANAmpFeel [--check]
juce_add_console_app(POMANAmpFeel PRODUCT_NAME "POMANAmpFeel")
target_sources(POMANAmpFeel PRIVATE Tests/AmpFeel.cpp)
target_include_directories(POMANAmpFeel PRIVATE Source)
target_compile_definitions(POMANAmpFeel PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)
target_link_libraries(POMANAmpFeel PRIVATE juce::juce_dsp PUBLIC juce::juce_recommended_config_flags juce::juce_recommended_lto_flags)
