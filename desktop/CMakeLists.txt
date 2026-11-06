# Teevee for the desktop: the robot's face in a Qt 6 window, with real controls and picture in picture (qt_main.cpp).
# The old SDL program is no longer part of this repository and is not built.
#
#   Linux / macOS:   cmake -S desktop -B build -DCMAKE_BUILD_TYPE=Release
#                    cmake --build build
#                    ./build/teevee-qt
#   Windows (Visual Studio, from the "x64 Native Tools Command Prompt"):
#                    cmake -S desktop -B build -G "Visual Studio 17 2022" -A x64
#                    cmake --build build --config Release
#                    build\Release\teevee-qt.exe
#
# SDL3 is used if it is installed (find_package). Otherwise CMake downloads SDL 3.4.18 from GitHub and builds it
# (needs git and an internet connection; about a minute). Force the download with -DTEEVEE_FETCH_SDL3=ON.
# Needs: CMake 3.16+, a C++17 compiler, Qt 6 (Widgets). Arch: pacman -S qt6-base.
cmake_minimum_required(VERSION 3.16)
project(teevee_desktop LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release)
endif()

option(TEEVEE_FETCH_SDL3 "Download and build SDL3 instead of using an installed one" OFF)

if(NOT TEEVEE_FETCH_SDL3)
  find_package(SDL3 CONFIG QUIET)
endif()
if(NOT SDL3_FOUND)
  message(STATUS "SDL3 not installed (or download forced): fetching SDL 3.4.18")
  include(FetchContent)
  FetchContent_Declare(SDL3
    GIT_REPOSITORY https://github.com/libsdl-org/SDL.git
    GIT_TAG release-3.4.18
    GIT_SHALLOW TRUE)
  set(SDL_SHARED OFF CACHE BOOL "" FORCE)
  set(SDL_STATIC ON CACHE BOOL "" FORCE)
  set(SDL_TEST_LIBRARY OFF CACHE BOOL "" FORCE)
  set(SDL_TESTS OFF CACHE BOOL "" FORCE)
  set(SDL_EXAMPLES OFF CACHE BOOL "" FORCE)
  FetchContent_MakeAvailable(SDL3)
endif()

set(SRC ${CMAKE_CURRENT_SOURCE_DIR}/../src/core)  # the shared core (no board code)

if(NOT CMAKE_CROSSCOMPILING)
  find_package(Qt6 COMPONENTS Widgets QUIET)
endif()
if(NOT Qt6_FOUND)
  message(FATAL_ERROR "teevee-qt needs Qt 6 (Widgets). Install it, or pass -DQt6_DIR=... to where Qt 6 is.")
endif()
if(Qt6_FOUND)
  message(STATUS "Qt ${Qt6_VERSION} found: building teevee-qt")
  add_executable(teevee-qt
    qt_main.cpp
    engine.cpp
    pet_os.cpp
    platform_lcd.cpp
    ${SRC}/renderer.cpp
    ${SRC}/render_common.cpp
    ${SRC}/render_fx.cpp
    ${SRC}/render_widgets.cpp
    ${SRC}/font_data.cpp)
  set_target_properties(teevee-qt PROPERTIES WIN32_EXECUTABLE TRUE)
  target_include_directories(teevee-qt PRIVATE ${SRC} ${CMAKE_CURRENT_SOURCE_DIR})
  target_compile_definitions(teevee-qt PRIVATE TEEVEE_MIND_N=128)
  target_link_libraries(teevee-qt PRIVATE Qt6::Widgets)
  if(TARGET SDL3::SDL3)  # (the save folder, the battery, pet_os.cpp's helpers: it opens no SDL window)
    target_link_libraries(teevee-qt PRIVATE SDL3::SDL3)
  else()
    target_link_libraries(teevee-qt PRIVATE SDL3::SDL3-static)
  endif()
  if(UNIX AND NOT APPLE)
    target_link_libraries(teevee-qt PRIVATE ${CMAKE_DL_LIBS})
  endif()
  if(WIN32)
    target_link_libraries(teevee-qt PRIVATE gdi32)
  endif()
  if(APPLE)
    target_link_libraries(teevee-qt PRIVATE "-framework CoreGraphics" "-framework CoreFoundation")
  endif()
  if(MSVC)
    target_compile_options(teevee-qt PRIVATE /W3 /utf-8)
    target_compile_definitions(teevee-qt PRIVATE _CRT_SECURE_NO_WARNINGS NOMINMAX)
  else()
    target_compile_options(teevee-qt PRIVATE -Wall -Wextra -Wno-unused-parameter)
  endif()
endif()
