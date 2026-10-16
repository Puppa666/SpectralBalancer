cmake_minimum_required(VERSION 3.20)
project(SpectralBalancer VERSION 1.0.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(FetchContent)
FetchContent_Declare(
    JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG        7.0.9
    GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(JUCE)

juce_add_plugin(SpectralBalancer
    COMPANY_NAME "OpenAudio"
    PLUGIN_MANUFACTURER_CODE OpAu
    PLUGIN_CODE SpBa
    FORMATS VST3 Standalone
    PRODUCT_NAME "Spectral Balancer"
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    COPY_PLUGIN_AFTER_BUILD FALSE
)

target_sources(SpectralBalancer PRIVATE
    Source/PluginProcessor.h
    Source/PluginProcessor.cpp
    Source/PluginEditor.h
    Source/PluginEditor.cpp
)

target_compile_definitions(SpectralBalancer PRIVATE
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
)

target_link_libraries(SpectralBalancer PRIVATE
    juce::juce_audio_utils
    juce::juce_dsp
)

juce_generate_juce_header(SpectralBalancer)
