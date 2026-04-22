cmake_minimum_required(VERSION 3.20)
project(Stack VERSION 1.0)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Ensure folders for outputs are nicely mapped
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

# ==============================================================================
# Dependency Management (Zero-Install Configuration via FetchContent)
# ==============================================================================
include(FetchContent)
set(FETCHCONTENT_UPDATES_DISCONNECTED ON)

set(STACK_DEPS_DIR "${CMAKE_BINARY_DIR}/_deps")
if (EXISTS "${STACK_DEPS_DIR}/glfw-src/CMakeLists.txt")
    set(FETCHCONTENT_SOURCE_DIR_GLFW "${STACK_DEPS_DIR}/glfw-src")
endif()
if (EXISTS "${STACK_DEPS_DIR}/imgui-src/imgui.h")
    set(FETCHCONTENT_SOURCE_DIR_IMGUI "${STACK_DEPS_DIR}/imgui-src")
endif()

# 1. GLFW
message(STATUS "Fetching GLFW...")
FetchContent_Declare(
    glfw
    GIT_REPOSITORY https://github.com/glfw/glfw.git
    GIT_TAG 3.4
)
set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(glfw)

# 2. ImGui
message(STATUS "Fetching Dear ImGui...")
FetchContent_Declare(
    imgui
    GIT_REPOSITORY https://github.com/ocornut/imgui.git
    GIT_TAG docking
)
FetchContent_MakeAvailable(imgui)

# (REMOVED FETCHCONTENT FOR JSON)

# ==============================================================================
# ImGui Core & Backends Compilation target since ImGui natively lacks CMakelists
# ==============================================================================
FetchContent_GetProperties(imgui)
add_library(imgui_core STATIC
    ${imgui_SOURCE_DIR}/imgui.cpp
    ${imgui_SOURCE_DIR}/imgui_draw.cpp
    ${imgui_SOURCE_DIR}/imgui_tables.cpp
    ${imgui_SOURCE_DIR}/imgui_widgets.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_glfw.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_opengl3.cpp
)
target_include_directories(imgui_core PUBLIC
    ${imgui_SOURCE_DIR}
    ${imgui_SOURCE_DIR}/backends
)
target_link_libraries(imgui_core PUBLIC glfw)

# ==============================================================================
# Our Application Structure
# ==============================================================================

# Automatically bake the splash image into a header if it changes
add_custom_target(BakeAssets
    COMMAND python "${CMAKE_CURRENT_SOURCE_DIR}/tools/bake_splash.py"
    WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
    COMMENT "Baking assets into C++..."
    VERBATIM
)

file(GLOB_RECURSE SRC_FILES CONFIGURE_DEPENDS
    "src/*.cpp"
    "src/*.h"
)

add_executable(${PROJECT_NAME} ${SRC_FILES})
add_dependencies(${PROJECT_NAME} BakeAssets)

target_include_directories(${PROJECT_NAME} PRIVATE
    src
    src/ThirdParty
)

target_link_libraries(${PROJECT_NAME} PRIVATE 
    glfw 
    imgui_core
)

# Link OpenGL based on platform
if (WIN32)
    target_link_libraries(${PROJECT_NAME} PRIVATE opengl32 comdlg32)
elseif (APPLE)
    target_link_libraries(${PROJECT_NAME} PRIVATE "-framework OpenGL")
elseif (UNIX)
    target_link_libraries(${PROJECT_NAME} PRIVATE GL)
endif()
