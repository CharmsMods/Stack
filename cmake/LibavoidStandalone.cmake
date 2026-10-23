# Corresponding source build: unmodified libavoid plus Stack's C boundary adapter.
# Source: https://github.com/mjwybrow/adaptagrams, commit 840ebcff20dbba36ad03a2160edf7cbaf9859984
# License: libavoid/LICENSE.LGPL (LGPL-2.1-or-later).
# cmake -S . -B build -A x64; cmake --build build --config Release
cmake_minimum_required(VERSION 3.20)
project(StackLibavoidSource LANGUAGES CXX)
if(MSVC)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()
file(GLOB sources CONFIGURE_DEPENDS libavoid/*.cpp)
add_library(libavoid SHARED ${sources} LibavoidBridge.cpp)
target_include_directories(libavoid PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_compile_definitions(libavoid PRIVATE STACK_AVOID_BUILD LIBAVOID_EXPORTS NOMINMAX _CRT_SECURE_NO_WARNINGS)
target_compile_features(libavoid PRIVATE cxx_std_17)
