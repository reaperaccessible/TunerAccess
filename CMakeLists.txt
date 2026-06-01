cmake_minimum_required(VERSION 3.22)

project(TunerAccess VERSION 1.0.0 LANGUAGES C CXX)

set(CMAKE_POSITION_INDEPENDENT_CODE ON)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(FetchContent)

# --- JUCE 8 ---
FetchContent_Declare(
    JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG        8.0.12
    GIT_SHALLOW    TRUE
)

FetchContent_MakeAvailable(JUCE)

# --- Plugin target ---
juce_add_plugin(TunerAccess
    COMPANY_NAME               "ReaperAccessible"
    PLUGIN_MANUFACTURER_CODE   RpAc
    PLUGIN_CODE                TuAc
    FORMATS                    VST3 Standalone
    PRODUCT_NAME               "TunerAccess"
    IS_SYNTH                   FALSE
    NEEDS_MIDI_INPUT           FALSE
    NEEDS_MIDI_OUTPUT          FALSE
    IS_MIDI_EFFECT             FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS TRUE
    COPY_PLUGIN_AFTER_BUILD    FALSE
    VST3_CATEGORIES            Fx Analyzer
    DESCRIPTION                "Accessible guitar tuner for screen reader users"
)

juce_generate_juce_header(TunerAccess)

target_sources(TunerAccess
    PRIVATE
        Source/PluginProcessor.cpp
        Source/PluginEditor.cpp
        Source/AccessibleComboDropdown.cpp
        Source/AccessibleSettingsPanel.cpp
        Source/CustomStandaloneApp.cpp
)

set_target_properties(TunerAccess PROPERTIES
    CXX_STANDARD 20
    CXX_STANDARD_REQUIRED ON
)

target_compile_definitions(TunerAccess
    PUBLIC
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0
        JUCE_DISPLAY_SPLASH_SCREEN=0
        JUCE_ACCESSIBILITY=1
        JUCE_MODAL_LOOPS_PERMITTED=1
        JUCE_WINDOWS_ALT_KEY_TRIGGERS_MENU=0
        JUCE_USE_CUSTOM_PLUGIN_STANDALONE_APP=1
        JUCE_ASIO=1
        NOMINMAX
        WIN32_LEAN_AND_MEAN
)

# ASIO SDK headers — required by JUCE_ASIO=1 to expose ASIO as a device type.
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/asiosdk/common")
    target_include_directories(TunerAccess PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/asiosdk/common")
endif()

# Async exception handling (parity with GuitarAccess)
if(MSVC)
    target_compile_options(TunerAccess PRIVATE "/EHa")
    target_compile_options(TunerAccess PRIVATE "$<$<CONFIG:Release>:/Zi>")
    target_link_options(TunerAccess PRIVATE "$<$<CONFIG:Release>:/DEBUG>" "$<$<CONFIG:Release>:/OPT:REF>" "$<$<CONFIG:Release>:/OPT:ICF>" "$<$<CONFIG:Release>:/MAP>")

    foreach(_target TunerAccess_VST3 TunerAccess_Standalone)
        if(TARGET ${_target})
            target_link_options(${_target} PRIVATE "$<$<CONFIG:Release>:/DEBUG>" "$<$<CONFIG:Release>:/OPT:REF>" "$<$<CONFIG:Release>:/OPT:ICF>" "$<$<CONFIG:Release>:/MAP>")
        endif()
    endforeach()
endif()

# Force defines onto the format targets directly — the JUCE plugin-client
# wrapper TUs compile inside the format target, and PUBLIC propagation from
# the shared-code target is not always honoured by older Visual Studio
# generators. Setting them again here is harmless and bullet-proof.
foreach(_target TunerAccess_VST3 TunerAccess_Standalone)
    if(TARGET ${_target})
        target_compile_definitions(${_target} PRIVATE
            JUCE_ACCESSIBILITY=1
            JUCE_USE_CUSTOM_PLUGIN_STANDALONE_APP=1
            JUCE_ASIO=1
        )
        if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/asiosdk/common")
            target_include_directories(${_target} PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/asiosdk/common")
        endif()
    endif()
endforeach()

target_link_libraries(TunerAccess
    PRIVATE
        juce::juce_audio_utils
        juce::juce_audio_formats
        juce::juce_audio_processors
        juce::juce_gui_basics
        juce::juce_gui_extra
        juce::juce_core
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags
)
