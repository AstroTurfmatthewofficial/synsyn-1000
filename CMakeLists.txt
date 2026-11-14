cmake_minimum_required(VERSION 3.22)
project(VSTForgeSynth VERSION 0.6.1 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(VSTFORGE_BUILD_TESTS "Build the offline render test tool" ON)

add_subdirectory(external/JUCE)

# ---------------------------------------------------------------------------
# Core: DSP + processor + editor sources, shared by the plugin and the tests.
# Kept as an INTERFACE library so each consumer compiles it with its own JUCE
# configuration (plugin-client vs. console app).
# ---------------------------------------------------------------------------
# Factory presets (human-readable .vfpreset XML) embedded via BinaryData.
file(GLOB_RECURSE VSTFORGE_FACTORY_PRESETS CONFIGURE_DEPENDS
     ${CMAKE_CURRENT_SOURCE_DIR}/presets/factory/*.vfpreset)
list(SORT VSTFORGE_FACTORY_PRESETS)
juce_add_binary_data(VSTForgeFactoryPresets
    HEADER_NAME VSTForgeFactoryPresets.h
    NAMESPACE VSTForgeFactoryPresets
    SOURCES ${VSTFORGE_FACTORY_PRESETS})

# Steinberg "VST Compatible" logo, embedded unaltered for the About box.
# VST is a registered trademark of Steinberg Media Technologies GmbH.
juce_add_binary_data(SynSynResources
    HEADER_NAME SynSynResources.h
    NAMESPACE SynSynResources
    SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/resources/VST_Compatible_Logo_Steinberg_negative.svg)

add_library(VSTForgeCore INTERFACE)
target_sources(VSTForgeCore INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}/Source/Parameters.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Source/dsp/SynthVoice.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Source/PluginProcessor.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Source/presets/PresetManager.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Source/gui/PresetBrowser.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Source/gui/ForgeLookAndFeel.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Source/gui/Components.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Source/gui/Sections.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Source/gui/KeyboardSection.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Source/gui/PluginEditor.cpp)
target_include_directories(VSTForgeCore INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/Source)
target_compile_definitions(VSTForgeCore INTERFACE VSTFORGE_VERSION_STRING=${PROJECT_VERSION})
target_link_libraries(VSTForgeCore INTERFACE
    VSTForgeFactoryPresets
    SynSynResources
    juce::juce_audio_utils
    juce::juce_dsp)

# ---------------------------------------------------------------------------
# Plugin
# ---------------------------------------------------------------------------
# Windows (MinGW-w64 cross-build, see cmake/mingw-w64-x86_64.cmake): VST3 + Standalone only.
# The VST3 moduleinfo.json manifest needs a helper run on the target OS, so it is skipped
# when cross-compiling (it is optional; hosts scan the plug-in instead).
if (WIN32)
    set(SYNSYN_FORMATS VST3 Standalone)
else()
    set(SYNSYN_FORMATS LV2 VST3 Standalone)
endif()
if (CMAKE_CROSSCOMPILING)
    set(SYNSYN_VST3_AUTO_MANIFEST FALSE)
else()
    set(SYNSYN_VST3_AUTO_MANIFEST TRUE)
endif()

juce_add_plugin(VSTForgeSynth
    # Rebrand (0.6.1): identifiers changed on purpose; old sessions will not find the plug-in.
    # Old: "VST Forge" / com.vstforge.synth / "VST Forge Synth" / Vfrg / Vfs1 / urn:vstforge:synth
    COMPANY_NAME "DoubleDouble Instrumentations"
    COMPANY_COPYRIGHT "Copyright (C) 2026 DoubleDouble Instrumentations. AGPL-3.0-or-later."
    BUNDLE_ID com.doubledouble.synsyn1000
    PRODUCT_NAME "SynSyn 1000"
    PLUGIN_MANUFACTURER_CODE Ddbl
    PLUGIN_CODE Ss1k
    IS_SYNTH TRUE
    NEEDS_MIDI_INPUT TRUE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    VST3_CATEGORIES Instrument Synth
    LV2URI "urn:doubledouble:synsyn1000"
    COPY_PLUGIN_AFTER_BUILD FALSE
    VST3_AUTO_MANIFEST ${SYNSYN_VST3_AUTO_MANIFEST}
    FORMATS ${SYNSYN_FORMATS})

target_sources(VSTForgeSynth PRIVATE
    Source/PluginEntry.cpp
    Source/standalone/StandaloneMidi.cpp)

target_compile_definitions(VSTForgeSynth PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0)

target_link_libraries(VSTForgeSynth
    PRIVATE
        VSTForgeCore
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)

# ---------------------------------------------------------------------------
# Offline render / check tool
# ---------------------------------------------------------------------------
if (VSTFORGE_BUILD_TESTS)
    juce_add_console_app(VSTForgeRenderTest PRODUCT_NAME "VSTForgeRenderTest")
    target_sources(VSTForgeRenderTest PRIVATE tests/RenderTest.cpp)
    target_compile_definitions(VSTForgeRenderTest PRIVATE
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_STANDALONE_APPLICATION=1
        VSTFORGE_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
    target_link_libraries(VSTForgeRenderTest
        PRIVATE
            VSTForgeCore
            juce::juce_recommended_config_flags
            juce::juce_recommended_warning_flags)
endif()
