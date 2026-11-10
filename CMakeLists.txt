cmake_minimum_required(VERSION 3.22)

project(ALLY_BASS VERSION 1.0.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Check for local JUCE or fetch
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/JUCE/CMakeLists.txt")
    add_subdirectory(JUCE)
else()
    include(FetchContent)
    FetchContent_Declare(
        JUCE
        GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
        GIT_TAG 8.0.4
        GIT_SHALLOW TRUE
    )
    FetchContent_MakeAvailable(JUCE)
endif()

juce_add_plugin(AllyBass
    COMPANY_NAME "JUICED UNICORN"
    IS_SYNTH TRUE
    NEEDS_MIDI_INPUT TRUE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS TRUE
    COPY_PLUGIN_AFTER_BUILD FALSE
    PLUGIN_MANUFACTURER_CODE JUnc
    PLUGIN_CODE AlBa
    FORMATS VST3 $<$<PLATFORM_ID:Darwin>:AU> Standalone
    PRODUCT_NAME "ALLY BASS"
    BUNDLE_ID "com.juicedunicorn.allybass"
)

target_sources(AllyBass PRIVATE
    Source/PluginProcessor.h
    Source/PluginProcessor.cpp
    Source/PluginEditor.h
    Source/PluginEditor.cpp
    Source/DSP/NapSubOscillator.h
    Source/DSP/PolderTransient.h
    Source/DSP/StadsdeelSaturation.h
    Source/DSP/WeerwaterGlide.h
    Source/DSP/MonoLockFilter.h
    Source/DSP/Overdrive036.h
    Source/GUI/BrutalistLookAndFeel.h
    Source/GUI/BrutalistLookAndFeel.cpp
    Source/GUI/NapWaveVisualizer.h
    Source/GUI/NapWaveVisualizer.cpp
    Source/GUI/HeavyKnob.h
    Source/GUI/BrutalistToggle.h
    Source/Presets/PresetManager.h
)

target_compile_definitions(AllyBass PRIVATE
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
)

target_link_libraries(AllyBass PRIVATE
    juce::juce_audio_utils
    juce::juce_audio_processors
    juce::juce_dsp
    juce::juce_gui_basics
    juce::juce_gui_extra
    juce::juce_recommended_config_flags
    juce::juce_recommended_warning_flags
)
