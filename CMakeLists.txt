cmake_minimum_required(VERSION 3.21)

project(noterad LANGUAGES CXX VERSION 1.0.0)

if(NOT MSVC)
    message(FATAL_ERROR "Noterad targets the Windows SDK through MSVC; no other toolchain is supported.")
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Statically linked CRT, so the executable runs with no redistributable. Set
# before the platform layer is pulled in, so it is built the same way.
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

set(CMAKE_CXX_FLAGS_DEBUG "/Od /Zi /RTC1 /D_DEBUG")
set(CMAKE_CXX_FLAGS_RELEASE "/Ox /Oi /Ot /Oy /GF /Gy /Zi /DNDEBUG")

# Develop against a sibling platform-h checkout when one exists; CI and a fresh
# clone fall back to the pinned revision below.
if(NOT DEFINED FETCHCONTENT_SOURCE_DIR_PLATFORM_H
   AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../platform-h/CMakeLists.txt")
    set(FETCHCONTENT_SOURCE_DIR_PLATFORM_H "${CMAKE_CURRENT_SOURCE_DIR}/../platform-h"
        CACHE PATH "Local platform-h checkout to build against")
endif()

include(FetchContent)
FetchContent_Declare(platform_h
    GIT_REPOSITORY https://github.com/ZacWalk/platform-h.git
    GIT_TAG        a2ef17b3e9be76325d058f2991dad4a59dce7b9d
)
FetchContent_MakeAvailable(platform_h)

platform_add_app(noterad
    SOURCES
        src/app.cpp
        src/app_commands.cpp
        src/commands.cpp
        src/document.cpp
        src/document_syntax.cpp
        src/tests.cpp
        src/util.cpp
    ICON        src/res/app.ico
    DESCRIPTION "Noterad text editor"
    OUTPUT_NAME noterad-64
)

target_include_directories(noterad PRIVATE src)
target_compile_options(noterad PRIVATE /W3 /MP)

target_link_options(noterad PRIVATE
    /DEBUG
    $<$<CONFIG:Release>:/OPT:REF>
    $<$<CONFIG:Release>:/OPT:ICF>
)

set_target_properties(noterad PROPERTIES
    DEBUG_POSTFIX d
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/exe"
    RUNTIME_OUTPUT_DIRECTORY_DEBUG "${CMAKE_SOURCE_DIR}/exe"
    RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_SOURCE_DIR}/exe"
)

target_precompile_headers(noterad PRIVATE src/pch.h)
