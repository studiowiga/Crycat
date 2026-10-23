cmake_minimum_required(VERSION 3.22)

# macOS : binaire universel (Apple Silicon + Intel), macOS 10.13 minimum (à fixer AVANT project())
if(APPLE)
  set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "" FORCE)
  set(CMAKE_OSX_DEPLOYMENT_TARGET "10.13" CACHE STRING "" FORCE)
endif()

project(Crycat VERSION 1.1.0)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
# Windows : runtime C++ statique (pas de redistribuable Visual C++ à installer)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

# JUCE : dossier local si JUCE_DIR est fourni, sinon téléchargement automatique
set(JUCE_DIR "" CACHE PATH "Copie locale de JUCE (optionnel)")
if(JUCE_DIR AND EXISTS "${JUCE_DIR}/CMakeLists.txt")
  add_subdirectory(${JUCE_DIR} JUCE)
else()
  include(FetchContent)
  FetchContent_Declare(JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG 8.0.4
    GIT_SHALLOW ON)
  FetchContent_MakeAvailable(JUCE)
endif()

# visuel de la pédale intégré au plugin (aucun fichier externe à l'exécution)
juce_add_binary_data(CrycatAssets SOURCES
  background.png treadle_down.png
  fs_on.png fs_off.png toggle_l.png toggle_r.png
  fonts/Poppins-Medium.ttf fonts/Poppins-Bold.ttf)

if(APPLE)
  set(CRYCAT_FORMATS VST3 AU)
else()
  set(CRYCAT_FORMATS VST3)
endif()

juce_add_plugin(Crycat
  COMPANY_NAME "Studio Wiga"
  PLUGIN_MANUFACTURER_CODE Wiga
  PLUGIN_CODE WgCc
  BUNDLE_ID com.studiowiga.crycat
  FORMATS ${CRYCAT_FORMATS}
  AU_MAIN_TYPE kAudioUnitType_MusicEffect
  VST3_CATEGORIES Fx Filter
  PRODUCT_NAME "CRYCAT"
  VERSION 1.1.0
  DESCRIPTION "Inductor wah pedal (Cry Baby-style) - double pedal format"
  IS_SYNTH FALSE
  NEEDS_MIDI_INPUT TRUE
  NEEDS_MIDI_OUTPUT FALSE
  IS_MIDI_EFFECT FALSE
  EDITOR_WANTS_KEYBOARD_FOCUS FALSE
  COPY_PLUGIN_AFTER_BUILD FALSE)

target_sources(Crycat PRIVATE
  PluginProcessor.cpp
  PluginEditor.cpp)

if(MSVC)
  target_compile_options(Crycat PRIVATE /utf-8)
endif()

target_compile_definitions(Crycat PUBLIC
  JUCE_WEB_BROWSER=0
  JUCE_USE_CURL=0
  JUCE_VST3_CAN_REPLACE_VST2=0
  JUCE_DISPLAY_SPLASH_SCREEN=0)

target_link_libraries(Crycat
  PRIVATE
    CrycatAssets
    juce::juce_audio_utils
  PUBLIC
    juce::juce_recommended_config_flags
    juce::juce_recommended_warning_flags)
