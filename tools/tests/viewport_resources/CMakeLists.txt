cmake_minimum_required(VERSION 3.16)
project(StackViewportResources LANGUAGES CXX)
# Independent focused checks; no graphics SDK or application dependency fetch.
# cmake -S tools/tests/viewport_resources -B _workspace/viewport-resources
# cmake --build _workspace/viewport-resources --config Release
# ctest --test-dir _workspace/viewport-resources -C Release --output-on-failure
include(CTest)
find_package(Threads REQUIRED)
get_filename_component(STACK_ROOT "${CMAKE_CURRENT_LIST_DIR}/../../.." ABSOLUTE)
add_executable(StackViewportResourceTests ../viewport_resources_tests.cpp)
target_include_directories(StackViewportResourceTests PRIVATE "${STACK_ROOT}/src")
target_compile_features(StackViewportResourceTests PRIVATE cxx_std_17)
target_link_libraries(StackViewportResourceTests PRIVATE Threads::Threads)
if(MSVC)
    target_compile_options(StackViewportResourceTests PRIVATE /W4 /permissive-)
else()
    target_compile_options(StackViewportResourceTests PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()
if(WIN32)
    target_sources(StackViewportResourceTests PRIVATE "${STACK_ROOT}/src/Renderer/ViewportSectionStore.cpp")
endif()
add_test(NAME StackRenderer.ViewportResources COMMAND StackViewportResourceTests)
set_tests_properties(StackRenderer.ViewportResources PROPERTIES TIMEOUT 30 LABELS "renderer;viewport;cpu")
