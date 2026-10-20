cmake_minimum_required(VERSION 3.22)
project(ModLanes VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

include(FetchContent)

FetchContent_Declare(JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG 8.0.15
    GIT_SHALLOW ON)

FetchContent_Declare(clap-juce-extensions
    GIT_REPOSITORY https://github.com/free-audio/clap-juce-extensions.git
    GIT_TAG 7adee3a1bd4684d4caa5601100e364abccff4b4d)

FetchContent_MakeAvailable(JUCE clap-juce-extensions)

juce_add_plugin(ModLanes
    COMPANY_NAME "Elias Naujoks"
    BUNDLE_ID com.eliasnaujoks.modlanes
    PLUGIN_MANUFACTURER_CODE Enau
    PLUGIN_CODE MdLn
    PRODUCT_NAME "Orrery"
    FORMATS VST3 Standalone
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT TRUE
    NEEDS_MIDI_OUTPUT TRUE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS TRUE
    COPY_PLUGIN_AFTER_BUILD FALSE)

# An audio effect (stereo pass-through + sidechain) so the follower lanes can
# hear audio; it still takes notes in and sends CC out.
clap_juce_extensions_plugin(TARGET ModLanes
    CLAP_ID "com.eliasnaujoks.modlanes"
    CLAP_FEATURES audio-effect note-effect utility)

target_sources(ModLanes PRIVATE
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp
    Source/gui/StepGrid.cpp
    Source/gui/StepGridPoints.cpp)

target_include_directories(ModLanes PRIVATE Source)

target_compile_definitions(ModLanes PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0)

target_link_libraries(ModLanes
    PRIVATE
        juce::juce_audio_utils
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)

# Optional benchmark (cmake -DMODLANES_BENCH=ON): times the audio engine and UI painting.
option(MODLANES_BENCH "Build the ModLanesBench performance test" OFF)
if(MODLANES_BENCH)
    juce_add_console_app(ModLanesBench PRODUCT_NAME "ModLanesBench")
    target_sources(ModLanesBench PRIVATE
        Source/bench/Benchmark.cpp
        Source/bench/LiveCheck.cpp
        Source/PluginProcessor.cpp
        Source/PluginEditor.cpp
        Source/gui/StepGrid.cpp
        Source/gui/StepGridPoints.cpp)
    target_include_directories(ModLanesBench PRIVATE Source)
    target_compile_definitions(ModLanesBench PRIVATE
        JucePlugin_Name="Orrery"
        MODLANES_PAINT_LOG=1
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0)
    target_link_libraries(ModLanesBench PRIVATE
        juce::juce_audio_utils
        juce::juce_recommended_config_flags)
endif()

# Copy the .clap into the per-user CLAP folder that Bitwig scans (no admin rights needed).
option(MODLANES_INSTALL_CLAP "Copy the .clap to the per-user CLAP folder after building" ON)
if(WIN32 AND MODLANES_INSTALL_CLAP)
    file(TO_CMAKE_PATH "$ENV{LOCALAPPDATA}/Programs/Common/CLAP" MODLANES_CLAP_DIR)
    add_custom_command(TARGET ModLanes_CLAP POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory "${MODLANES_CLAP_DIR}"
        COMMAND ${CMAKE_COMMAND} -E copy "$<TARGET_FILE:ModLanes_CLAP>" "${MODLANES_CLAP_DIR}/"
        VERBATIM)
endif()
