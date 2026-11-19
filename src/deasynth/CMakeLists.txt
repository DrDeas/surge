# vi:set sw=2 et:
#
# Deasynth: a standalone application and plugin with its own interface, built on the
# Surge XT engine (surge-common).
#
#   macOS    Standalone app, Audio Unit (for Logic Pro, GarageBand, MainStage), VST3
#   others   Standalone app, VST3
#
# Signing identity, bundle identifiers and plugin codes are set here.
project(deasynth VERSION ${CMAKE_PROJECT_VERSION})

set(DEASYNTH_BUNDLE_ID "com.eldrin.deasynth" CACHE STRING "Bundle identifier for Deasynth")
set(DEASYNTH_TEAM_ID "H7CJSVP2JC" CACHE STRING "Apple Developer Team ID used when signing Deasynth")

set(DEASYNTH_FORMATS Standalone)
if(NOT SURGE_SKIP_VST3)
  list(APPEND DEASYNTH_FORMATS VST3)
endif()
if(APPLE AND NOT CMAKE_CROSSCOMPILING)
  list(APPEND DEASYNTH_FORMATS AU)
endif()
message(STATUS "Building Deasynth as: ${DEASYNTH_FORMATS}")

juce_add_binary_data(deasynth-binary
  NAMESPACE DeasynthBinary
  HEADER_NAME DeasynthBinary.h
  SOURCES
  ${CMAKE_SOURCE_DIR}/resources/fonts/Lato-Regular.ttf
  ${CMAKE_SOURCE_DIR}/resources/fonts/Lato-Bold.ttf
)

juce_add_plugin(${PROJECT_NAME}
  PRODUCT_NAME "Deasynth"
  COMPANY_NAME "Eldrin"
  BUNDLE_ID "${DEASYNTH_BUNDLE_ID}"

  # Four character codes. The manufacturer code must contain an upper case letter and the
  # pair must be unique to this plugin: Logic identifies the Audio Unit as "aumu Deas Eldr".
  PLUGIN_MANUFACTURER_CODE Eldr
  PLUGIN_CODE Deas

  ICON_BIG "${CMAKE_SOURCE_DIR}/resources/deasynth/DeasynthIcon.png"

  IS_SYNTH TRUE
  NEEDS_MIDI_INPUT TRUE
  NEEDS_MIDI_OUTPUT FALSE
  IS_MIDI_EFFECT FALSE
  EDITOR_WANTS_KEYBOARD_FOCUS TRUE

  BLUETOOTH_PERMISSION_ENABLED TRUE
  BLUETOOTH_PERMISSION_TEXT "Deasynth uses Bluetooth to connect to wireless MIDI controllers"

  COPY_PLUGIN_AFTER_BUILD ${SURGE_COPY_AFTER_BUILD}

  VST3_CATEGORIES Instrument Synth
  VST3_AUTO_MANIFEST FALSE

  AU_MAIN_TYPE kAudioUnitType_MusicDevice
  AU_SANDBOX_SAFE TRUE
  HARDENED_RUNTIME_ENABLED TRUE

  FORMATS ${DEASYNTH_FORMATS}
)

target_sources(${PROJECT_NAME} PRIVATE
  DeasynthProcessor.cpp
  DeasynthProcessor.h
  dsp/AnalogStage.h
  gui/DeasynthEditor.cpp
  gui/DeasynthEditor.h
  gui/Displays.cpp
  gui/Displays.h
  gui/Overlays.cpp
  gui/Overlays.h
  gui/Panels.cpp
  gui/Panels.h
  gui/Theme.cpp
  gui/Theme.h
  gui/Widgets.cpp
  gui/Widgets.h
)

target_compile_definitions(${PROJECT_NAME} PUBLIC
  $<IF:$<CONFIG:DEBUG>,BUILD_IS_DEBUG,BUILD_IS_RELEASE>=1
  JUCE_STANDALONE_FILTER_WINDOW_USE_KIOSK_MODE=0
)

target_link_libraries(${PROJECT_NAME} PRIVATE
  surge-common
  surge-juce
  juce::juce_graphics
  juce::juce_audio_utils
  juce::juce_audio_processors
  juce::juce_audio_plugin_client
  juce::juce_gui_basics
  juce::juce_dsp
  deasynth-binary
  sst-filters-extras
)

target_include_directories(${PROJECT_NAME} PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

# Xcode signs the bundles itself when given the team; other generators are signed
# afterwards by scripts/deasynth/build-mac.sh.
if(APPLE AND CMAKE_GENERATOR STREQUAL "Xcode")
  foreach(fmt ${DEASYNTH_FORMATS})
    if(TARGET ${PROJECT_NAME}_${fmt})
      set_target_properties(${PROJECT_NAME}_${fmt} PROPERTIES
        XCODE_ATTRIBUTE_DEVELOPMENT_TEAM "${DEASYNTH_TEAM_ID}"
        XCODE_ATTRIBUTE_CODE_SIGN_STYLE "Automatic")
    endif()
  endforeach()
endif()

surge_juce_package(${PROJECT_NAME} "Deasynth")

# A console self-test of the Deasynth wrapper: audio path, session state, the output stage,
# voice variance, and that every editor page builds and paints.
#   cmake --build build --target deasynth-selftest && build/deasynth-selftest
option(DEASYNTH_BUILD_SELFTEST "Build the Deasynth self-test executable" ON)
if(DEASYNTH_BUILD_SELFTEST)
  juce_add_console_app(deasynth-selftest PRODUCT_NAME "deasynth-selftest")
  target_sources(deasynth-selftest PRIVATE
    tests/selftest.cpp
    tests/presetgen.cpp
    DeasynthProcessor.cpp
    gui/DeasynthEditor.cpp
    gui/Displays.cpp
    gui/Overlays.cpp
    gui/Panels.cpp
    gui/Theme.cpp
    gui/Widgets.cpp
  )
  target_compile_definitions(deasynth-selftest PRIVATE
    JUCE_STANDALONE_APPLICATION=1
    JucePlugin_Name="Deasynth"
    JucePlugin_Build_Standalone=1
  )
  target_link_libraries(deasynth-selftest PRIVATE
    surge-common
    surge-juce
    juce::juce_graphics
    juce::juce_audio_utils
    juce::juce_audio_processors
    juce::juce_gui_basics
    juce::juce_dsp
    deasynth-binary
    sst-filters-extras
  )
  target_include_directories(deasynth-selftest PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
  set_target_properties(deasynth-selftest PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})
  if(SURGE_BUILD_TESTRUNNER)
    add_test(NAME deasynth-selftest COMMAND deasynth-selftest)
  endif()
endif()
