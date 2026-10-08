cmake_minimum_required(VERSION 3.22)

if(APPLE)
    set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "Universal binary")
    set(CMAKE_OSX_DEPLOYMENT_TARGET "10.13" CACHE STRING "Minimum macOS")
endif()

project(ScopeLab VERSION 0.1.0)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Usa la carpeta JUCE/ si existe; si no, la descarga automáticamente.
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/JUCE/CMakeLists.txt")
    add_subdirectory(JUCE)
else()
    include(FetchContent)
    FetchContent_Declare(JUCE
        GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
        GIT_TAG 8.0.4
        GIT_SHALLOW ON)
    FetchContent_MakeAvailable(JUCE)
endif()

set(SCOPELAB_FORMATS VST3 Standalone)
if(APPLE)
    list(APPEND SCOPELAB_FORMATS AU)
endif()

juce_add_plugin(ScopeLab
    COMPANY_NAME "ScopeLab"
    BUNDLE_ID "com.scopelab.scopelab"
    PLUGIN_MANUFACTURER_CODE Sclb
    PLUGIN_CODE Scl1
    FORMATS ${SCOPELAB_FORMATS}
    PRODUCT_NAME "ScopeLab"
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    VST3_CATEGORIES Fx Analyzer
    AU_MAIN_TYPE kAudioUnitType_Effect
    COPY_PLUGIN_AFTER_BUILD FALSE)

target_sources(ScopeLab PRIVATE
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp)

target_compile_definitions(ScopeLab PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0)

target_link_libraries(ScopeLab
    PRIVATE
        juce::juce_audio_utils
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)

# Herramienta opcional: genera capturas de la interfaz con una señal de prueba.
option(SCOPELAB_BUILD_SNAPSHOT "Build the screenshot tool" OFF)
if(SCOPELAB_BUILD_SNAPSHOT)
    juce_add_console_app(ScopeSnapshot PRODUCT_NAME "ScopeSnapshot")
    target_sources(ScopeSnapshot PRIVATE
        Tools/SnapshotMain.cpp
        Source/PluginProcessor.cpp
        Source/PluginEditor.cpp)
    target_compile_definitions(ScopeSnapshot PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)
    target_link_libraries(ScopeSnapshot PRIVATE
        juce::juce_audio_utils juce::juce_dsp
        juce::juce_recommended_config_flags)
endif()
