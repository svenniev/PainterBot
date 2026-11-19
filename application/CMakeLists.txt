cmake_minimum_required(VERSION 3.28)
project(PainterBot VERSION 0.1.0 LANGUAGES C CXX)

include(CTest)
include(FetchContent)

set(wxBUILD_SHARED OFF CACHE BOOL "" FORCE)
set(wxBUILD_MONOLITHIC OFF CACHE BOOL "" FORCE)
set(wxBUILD_SAMPLES OFF CACHE BOOL "" FORCE)
set(wxBUILD_TESTS OFF CACHE BOOL "" FORCE)
set(wxBUILD_DEMOS OFF CACHE BOOL "" FORCE)
set(wxBUILD_INSTALL OFF CACHE BOOL "" FORCE)
set(wxUSE_WEBVIEW OFF CACHE BOOL "" FORCE)
FetchContent_Declare(wxwidgets
    URL https://github.com/wxWidgets/wxWidgets/releases/download/v3.2.12/wxWidgets-3.2.12.zip
    URL_HASH SHA256=1f7ec935dddbba5cf454795dc255ef1e21c668533f5a5951a6034f2949c80433
    EXCLUDE_FROM_ALL
)
FetchContent_MakeAvailable(wxwidgets)

add_library(painterbot_core STATIC
    src/algo/VectorAlgorithm.cpp
    src/algo/Palette.cpp
    src/commands/CommandProcessor.cpp
    src/viewmodel/viewmodel.cpp
)
target_include_directories(painterbot_core PUBLIC src)
target_compile_features(painterbot_core PUBLIC cxx_std_20)

add_library(painterbot_image_io STATIC src/io/ImageLoader.cpp)
target_link_libraries(painterbot_image_io PUBLIC painterbot_core PRIVATE wx::core)

add_executable(PainterBot WIN32 src/ui/PainterBotApp.cpp src/ui/VectorPanel.cpp)
if(WIN32)
    target_sources(PainterBot PRIVATE src/ui/PainterBot.rc)
    if(MSVC)
        target_link_options(PainterBot PRIVATE /MANIFEST:NO)
    endif()
endif()
set_target_properties(PainterBot PROPERTIES OUTPUT_NAME "PainterBot Application")
target_link_libraries(PainterBot PRIVATE painterbot_image_io wx::core)

add_executable(PainterBotCommand src/commands/CommandMain.cpp)
target_link_libraries(PainterBotCommand PRIVATE painterbot_image_io wx::core)

if(BUILD_TESTING)
    add_executable(PainterBotTests tests/PainterBotTests.cpp)
    target_link_libraries(PainterBotTests PRIVATE painterbot_image_io wx::core)
    add_test(NAME painterbot
        COMMAND PainterBotTests "${CMAKE_CURRENT_SOURCE_DIR}/tests/res/above-clouds-mountains-peak-sunrise-moon-winter-cold-scenic-3440x1440-4569.jpg")
endif()
