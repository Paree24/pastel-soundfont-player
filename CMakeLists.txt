cmake_minimum_required(VERSION 3.22)
project(PastelSoundfontPlayer VERSION 1.0.0)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(FetchContent)

# Reuse the JUCE checkout already fetched for ERSA when present so we don't
# re-download ~100MB. Falls back to a fresh fetch otherwise.
set(ERSA_JUCE_SRC "/home/paree/ai_experiments/ersa/build/_deps/juce-src")
if(EXISTS "${ERSA_JUCE_SRC}/CMakeLists.txt")
  message(STATUS "Reusing local JUCE from ${ERSA_JUCE_SRC}")
  FetchContent_Declare(JUCE SOURCE_DIR "${ERSA_JUCE_SRC}")
else()
  FetchContent_Declare(
    JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG 8.0.6
    GIT_SHALLOW TRUE
  )
endif()
FetchContent_MakeAvailable(JUCE)

juce_add_binary_data(PastelAssets
  HEADER_NAME PastelAssets.h
  NAMESPACE PastelAssets
  SOURCES
    Assets/Lato-Regular.ttf
    Assets/Lato-Bold.ttf
    Assets/Lato-Black.ttf
)

juce_add_plugin(PastelPlayer
  COMPANY_NAME "Pastel Soundfont Player"
  BUNDLE_ID "com.pastelsoundfontplayer.pastelplayer"
  PLUGIN_MANUFACTURER_CODE Past
  PLUGIN_CODE Psp1
  FORMATS VST3 Standalone
  PRODUCT_NAME "Pastel Soundfont Player"
  DESCRIPTION "Simple SF2/SFZ player with filter, LFO, FX, arp and library browser"
  NEEDS_MIDI_INPUT TRUE
  NEEDS_MIDI_OUTPUT FALSE
  IS_SYNTH TRUE
  COPY_PLUGIN_AFTER_BUILD FALSE
)

juce_generate_juce_header(PastelPlayer)

target_sources(PastelPlayer PRIVATE
  Source/PluginProcessor.cpp
  Source/PluginEditor.cpp
  Source/PastelLookAndFeel.cpp
  Source/SamplerEngine.cpp
  Source/TsfImpl.cpp
  Source/DSPEngine.cpp
  Source/FileLibrary.cpp
  Source/Parameters.cpp
  Source/PianoStrip.cpp
)

target_include_directories(PastelPlayer PRIVATE Source)

target_compile_definitions(PastelPlayer PRIVATE
  JUCE_WEB_BROWSER=0
  JUCE_USE_CURL=0
  JUCE_VST3_CAN_REPLACE_VST2=0
)

target_link_libraries(PastelPlayer PRIVATE
  juce::juce_audio_utils
  juce::juce_dsp
  juce::juce_data_structures
  juce_audio_plugin_client
  PastelAssets
)

# Headless smoke-test harness (links the real plugin sources)
juce_add_console_app(PastelHarness PRODUCT_NAME "PastelHarness")
target_sources(PastelHarness PRIVATE
  Source/PluginProcessor.cpp
  Source/PluginEditor.cpp
  Source/PastelLookAndFeel.cpp
  Source/SamplerEngine.cpp
  Source/TsfImpl.cpp
  Source/DSPEngine.cpp
  Source/FileLibrary.cpp
  Source/Parameters.cpp
  Source/PianoStrip.cpp
  Test/Harness.cpp
)
target_include_directories(PastelHarness PRIVATE Source)
target_compile_definitions(PastelHarness PRIVATE
  JUCE_WEB_BROWSER=0
  JUCE_USE_CURL=0
  JUCE_VST3_CAN_REPLACE_VST2=0
)
target_link_libraries(PastelHarness PRIVATE
  juce::juce_audio_utils
  juce::juce_dsp
  juce::juce_data_structures
  PastelAssets
)
