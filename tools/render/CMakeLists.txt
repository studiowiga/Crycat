# Outil hors plugin : compile le processeur + la fenêtre de CRYCAT sous Linux et exporte des captures PNG (aperçu, notice)
cmake_minimum_required(VERSION 3.22)
project(CrycatRender VERSION 1.0.0)
set(CMAKE_CXX_STANDARD 17)
add_subdirectory(${JUCE_DIR} JUCE)
set(W ${CMAKE_CURRENT_SOURCE_DIR}/../..)
juce_add_binary_data(CrycatAssets SOURCES
  ${W}/background.png ${W}/treadle_down.png
  ${W}/fs_off.png ${W}/fs_on.png ${W}/led_glow.png ${W}/toggle_l.png ${W}/toggle_m.png ${W}/toggle_r.png
  ${W}/fonts/Poppins-Medium.ttf ${W}/fonts/Poppins-Bold.ttf)
juce_add_console_app(CrycatRender PRODUCT_NAME "CrycatRender")
target_sources(CrycatRender PRIVATE main.cpp ${W}/PluginProcessor.cpp ${W}/PluginEditor.cpp)
target_include_directories(CrycatRender PRIVATE ${W})
target_compile_definitions(CrycatRender PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_ALSA=0 JUCE_JACK=0
  JUCE_USE_XRANDR=0 JUCE_USE_XINERAMA=0 JUCE_USE_XCURSOR=0 JucePlugin_Name="CRYCAT" JucePlugin_Build_VST3=0
  JUCE_STANDALONE_APPLICATION=1 JUCE_MODAL_LOOPS_PERMITTED=1)
target_link_libraries(CrycatRender PRIVATE CrycatAssets juce::juce_audio_utils juce::juce_recommended_config_flags)
