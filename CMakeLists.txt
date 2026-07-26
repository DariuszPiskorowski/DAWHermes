cmake_minimum_required(VERSION 3.22)

project(DAWHermes VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include(CompilerWarnings)

include(CTest)
enable_testing()

include(FetchContent)
set(FETCHCONTENT_UPDATES_DISCONNECTED ON)
set(JUCE_BUILD_EXAMPLES OFF CACHE BOOL "Disable JUCE examples" FORCE)
set(JUCE_BUILD_EXTRAS OFF CACHE BOOL "Disable JUCE extras" FORCE)
set(JUCE_ENABLE_MODULE_SOURCE_GROUPS ON)

FetchContent_Declare(
  juce
  GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
  GIT_TAG 8.0.13
  GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(juce)

add_library(dawhermes_core
  src/core/ProjectModel.cpp
  src/core/ProjectController.cpp
)
target_include_directories(dawhermes_core PUBLIC src)
set_project_warnings(dawhermes_core)

add_library(dawhermes_hermes
  src/hermes/HermesValidation.cpp
  src/hermes/StubHermesEngine.cpp
  src/hermes/HermesCommandAvailability.cpp
)
target_include_directories(dawhermes_hermes PUBLIC src)
target_link_libraries(dawhermes_hermes PUBLIC dawhermes_core)
set_project_warnings(dawhermes_hermes)

add_library(dawhermes_ui
  src/ui/MainComponent.cpp
  src/ui/HermesDialogs.cpp
  src/ui/DrumMappingDialog.cpp
  src/app/MainWindow.cpp
  src/app/AppLogger.cpp
  src/app/MainApplication.cpp
)
target_include_directories(dawhermes_ui PUBLIC src)
target_link_libraries(dawhermes_ui
  PUBLIC dawhermes_core dawhermes_hermes
  PRIVATE juce::juce_gui_extra)
target_compile_definitions(
  dawhermes_ui
  PRIVATE
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_MODAL_LOOPS_PERMITTED=1)
set_project_warnings(dawhermes_ui)

juce_add_gui_app(DAWHermes
  COMPANY_NAME DAWHermes
  PRODUCT_NAME "DAWHermes"
)

target_sources(DAWHermes PRIVATE src/app/Main.cpp)

target_link_libraries(DAWHermes
  PRIVATE
    dawhermes_ui
    juce::juce_gui_extra
    juce::juce_recommended_config_flags
    juce::juce_recommended_lto_flags)

target_compile_definitions(
  DAWHermes
  PRIVATE
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_MODAL_LOOPS_PERMITTED=1)
set_project_warnings(DAWHermes)

add_executable(DAWHermesTests
  tests/test_main.cpp
)
target_link_libraries(DAWHermesTests PRIVATE dawhermes_core dawhermes_hermes)
set_project_warnings(DAWHermesTests)

add_test(NAME DAWHermesTests COMMAND DAWHermesTests)
