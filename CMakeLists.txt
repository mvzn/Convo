cmake_minimum_required(VERSION 3.22)

project(Convo VERSION 1.2.0 LANGUAGES C CXX)

# Default to Release for single-config generators (Ninja, Makefiles): an unoptimised Debug
# build stutters / drops out on long-IR convolution. Multi-config generators (VS, Xcode) are
# unaffected. Override with -DCMAKE_BUILD_TYPE=Debug or RelWithDebInfo.
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_OSX_DEPLOYMENT_TARGET "10.13" CACHE STRING "Minimum macOS version")

# ---------------------------------------------------------------------------
# JUCE via FetchContent (pinned, reproducible). JUCE lives in build/, gitignored.
# ---------------------------------------------------------------------------
include(FetchContent)
FetchContent_Declare(
    JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG        8.0.6
    GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(JUCE)

# AU only builds on macOS; on Linux/Windows it is simply omitted.
set(CONVO_FORMATS VST3)
if(APPLE)
    list(APPEND CONVO_FORMATS AU)
endif()

juce_add_plugin(Convo
    PRODUCT_NAME             "Convo"
    COMPANY_NAME             "mvzn"
    BUNDLE_ID                "com.mvzn.Convo"
    PLUGIN_MANUFACTURER_CODE "Mvzn"
    PLUGIN_CODE              "Cnvo"
    FORMATS                  ${CONVO_FORMATS}
    IS_SYNTH                 FALSE
    NEEDS_MIDI_INPUT         FALSE
    NEEDS_MIDI_OUTPUT        FALSE
    IS_MIDI_EFFECT           FALSE
    VST3_CATEGORIES          "Fx" "Reverb"
    AU_MAIN_TYPE             "kAudioUnitType_Effect"
    COPY_PLUGIN_AFTER_BUILD  TRUE
)

juce_generate_juce_header(Convo)

target_sources(Convo PRIVATE
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp
    Source/IRLibrary.cpp
    Source/ConvolutionEngine.cpp
    Source/ConvoLookAndFeel.cpp
)

target_compile_definitions(Convo PRIVATE
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_USE_MP3AUDIOFORMAT=1     # enable .mp3 decoding
    JUCE_USE_OGGVORBIS=1          # .ogg (default on, explicit for clarity)
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_STRICT_REFCOUNTEDPOINTER=1
)

target_link_libraries(Convo
    PRIVATE
        juce::juce_audio_utils
        juce::juce_dsp
        juce::juce_gui_extra
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags
)
