cmake_minimum_required(VERSION 3.22)

project(Tare VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

option(TARE_BUILD_CLAP "Also build a CLAP plugin via clap-juce-extensions" ON)
option(TARE_BUILD_TESTS "Build the offline latency probe test" ON)

include(FetchContent)

FetchContent_Declare(JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG        9.0.4
    GIT_SHALLOW    ON)
FetchContent_MakeAvailable(JUCE)

if(TARE_BUILD_CLAP)
    FetchContent_Declare(clap-juce-extensions
        GIT_REPOSITORY https://github.com/free-audio/clap-juce-extensions.git
        GIT_TAG        7adee3a1bd4684d4caa5601100e364abccff4b4d)
    FetchContent_MakeAvailable(clap-juce-extensions)
endif()

# The same code is built as two plugins, because hosts disagree about where
# audio inputs and MIDI may go:
#  - an instrument whose synth return is a sidechain input (Ableton, Bitwig, ...)
#  - an audio effect whose synth return is its main input (FL Studio, which
#    can't route audio into instruments but can send MIDI to effects)
function(tare_add_plugin target)
    cmake_parse_arguments(ARG "" "PRODUCT_NAME;PLUGIN_CODE;IS_SYNTH;CLAP_ID" "VST3_CATEGORIES;CLAP_FEATURES" ${ARGN})

    juce_add_plugin(${target}
        COMPANY_NAME             "kay"
        PRODUCT_NAME             "${ARG_PRODUCT_NAME}"
        PLUGIN_MANUFACTURER_CODE Kmfz
        PLUGIN_CODE              ${ARG_PLUGIN_CODE}
        FORMATS                  VST3
        IS_SYNTH                 ${ARG_IS_SYNTH}
        NEEDS_MIDI_INPUT         TRUE
        NEEDS_MIDI_OUTPUT        TRUE
        IS_MIDI_EFFECT           FALSE
        EDITOR_WANTS_KEYBOARD_FOCUS FALSE
        VST3_CATEGORIES          ${ARG_VST3_CATEGORIES}
        COPY_PLUGIN_AFTER_BUILD  FALSE)

    target_sources(${target} PRIVATE
        Source/LatencyProbe.cpp
        Source/PluginProcessor.cpp
        Source/PluginEditor.cpp)

    target_compile_definitions(${target} PUBLIC
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0
        JUCE_DISPLAY_SPLASH_SCREEN=0)

    target_link_libraries(${target}
        PRIVATE
            juce::juce_audio_utils
        PUBLIC
            juce::juce_recommended_config_flags
            juce::juce_recommended_lto_flags
            juce::juce_recommended_warning_flags)

    if(TARE_BUILD_CLAP)
        clap_juce_extensions_plugin(TARGET ${target}
            CLAP_ID       "${ARG_CLAP_ID}"
            CLAP_FEATURES ${ARG_CLAP_FEATURES})

        target_link_libraries(${target} PRIVATE clap_juce_extensions)
        target_compile_definitions(${target} PRIVATE TARE_HAS_CLAP=1)
    endif()
endfunction()

# PLUGIN_CODE and CLAP_ID predate the rename to Tare. Hosts identify plugins by
# them, so changing either breaks existing projects.
tare_add_plugin(Tare
    PRODUCT_NAME    "Tare"
    PLUGIN_CODE     Apch
    IS_SYNTH        TRUE
    VST3_CATEGORIES Instrument External
    CLAP_ID         "com.kay.audiopatcher"
    CLAP_FEATURES   instrument utility)

tare_add_plugin(TareFX
    PRODUCT_NAME    "Tare FX"
    PLUGIN_CODE     Apfx
    IS_SYNTH        FALSE
    VST3_CATEGORIES Fx External
    CLAP_ID         "com.kay.audiopatcher.fx"
    CLAP_FEATURES   audio-effect utility)

if(TARE_BUILD_TESTS)
    juce_add_console_app(ProbeTest PRODUCT_NAME "ProbeTest")
    target_sources(ProbeTest PRIVATE Tests/ProbeTest.cpp Source/LatencyProbe.cpp)
    target_compile_definitions(ProbeTest PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)
    target_link_libraries(ProbeTest
        PRIVATE
            juce::juce_audio_basics
        PUBLIC
            juce::juce_recommended_config_flags
            juce::juce_recommended_warning_flags)

    enable_testing()
    add_test(NAME ProbeTest COMMAND ProbeTest)
endif()
