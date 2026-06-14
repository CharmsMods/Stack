cmake_minimum_required(VERSION 3.20)
project(Stack VERSION 1.0)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Ensure folders for outputs are nicely mapped directly to the build folder
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_RELEASE ${CMAKE_BINARY_DIR})
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_DEBUG ${CMAKE_BINARY_DIR})
if (MSVC)
    # Use the static MSVC runtime so the portable build does not require a separate VC++ redistributable.
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

# Dependency Management (Zero-Install Configuration via FetchContent)
# ==============================================================================
find_package(Python3 REQUIRED COMPONENTS Interpreter)
include(FetchContent)
set(FETCHCONTENT_UPDATES_DISCONNECTED ON)
option(STACK_ENABLE_LIBRAW "Enable LibRaw-backed Sony ARW RAW loading" ON)

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

# 3. LibRaw (RAW file unpacking / metadata only)
if (STACK_ENABLE_LIBRAW)
    message(STATUS "Fetching LibRaw...")
    FetchContent_Declare(
        libraw
        GIT_REPOSITORY https://github.com/LibRaw/LibRaw.git
        GIT_TAG b860248a89d9082b8e0a1e202e516f46af9adb29
    )
    FetchContent_MakeAvailable(libraw)
    FetchContent_GetProperties(libraw)
    file(GLOB_RECURSE STACK_LIBRAW_SOURCES CONFIGURE_DEPENDS
        "${libraw_SOURCE_DIR}/src/*.cpp"
    )
    list(FILTER STACK_LIBRAW_SOURCES EXCLUDE REGEX "_ph\\.cpp$")
    add_library(stack_libraw SHARED ${STACK_LIBRAW_SOURCES})
    set_target_properties(stack_libraw PROPERTIES
        OUTPUT_NAME libraw
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
        RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_BINARY_DIR}"
        RUNTIME_OUTPUT_DIRECTORY_DEBUG "${CMAKE_BINARY_DIR}"
        ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/libraw-import"
        ARCHIVE_OUTPUT_DIRECTORY_RELEASE "${CMAKE_BINARY_DIR}/libraw-import"
        ARCHIVE_OUTPUT_DIRECTORY_DEBUG "${CMAKE_BINARY_DIR}/libraw-import"
    )
    target_include_directories(stack_libraw PUBLIC
        "${libraw_SOURCE_DIR}"
    )
    target_compile_definitions(stack_libraw PRIVATE LIBRAW_BUILDLIB)
    if (MSVC)
        target_compile_options(stack_libraw PRIVATE /wd4244 /wd4267 /wd4996)
    endif()
endif()

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
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tools/bake_splash.py"
    WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
    COMMENT "Baking assets into C++..."
    VERBATIM
)

add_custom_target(BakeTabIcons
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tools/bake_tab_icons.py"
    WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
    COMMENT "Baking tab icons into C++..."
    VERBATIM
)

add_custom_target(BakeFonts
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tools/bake_fonts.py"
    WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
    COMMENT "Baking fonts into C++..."
    VERBATIM
)

file(GLOB_RECURSE SRC_FILES CONFIGURE_DEPENDS
    "src/*.cpp"
    "src/*.h"
)

if (WIN32)
    list(APPEND SRC_FILES "src/resources.rc")
endif()

# Legacy aggregate layer implementations were replaced by split, registry-backed
# nodes. Keep the files in the tree for migration reference, but do not compile them.
list(FILTER SRC_FILES EXCLUDE REGEX "/src/Editor/Layers/(AdjustmentsLayer|BlurLayer|CompressionLayer|CorruptionLayer|CropTransformLayer|DenoisingLayer|DitherLayer|EdgeEffectsLayer|HeatwaveLayer)\\.(cpp|h)$")

add_executable(${PROJECT_NAME} ${SRC_FILES})
add_dependencies(${PROJECT_NAME} BakeAssets BakeTabIcons BakeFonts)

target_include_directories(${PROJECT_NAME} PRIVATE
    ${CMAKE_BINARY_DIR}/generated/src
    src
    src/ThirdParty
)

target_link_libraries(${PROJECT_NAME} PRIVATE 
    glfw 
    imgui_core
)

if (STACK_ENABLE_LIBRAW)
    target_compile_definitions(${PROJECT_NAME} PRIVATE STACK_ENABLE_LIBRAW=1)
    target_link_libraries(${PROJECT_NAME} PRIVATE stack_libraw)
    add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_CURRENT_SOURCE_DIR}/THIRD_PARTY_NOTICES.md"
            "$<TARGET_FILE_DIR:${PROJECT_NAME}>/THIRD_PARTY_NOTICES.md"
        COMMAND ${CMAKE_COMMAND} -E make_directory
            "$<TARGET_FILE_DIR:${PROJECT_NAME}>/licenses"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${libraw_SOURCE_DIR}/LICENSE.LGPL"
            "$<TARGET_FILE_DIR:${PROJECT_NAME}>/licenses/LibRaw-LGPL-2.1.txt"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${libraw_SOURCE_DIR}/LICENSE.CDDL"
            "$<TARGET_FILE_DIR:${PROJECT_NAME}>/licenses/LibRaw-CDDL-1.0.txt"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${libraw_SOURCE_DIR}/COPYRIGHT"
            "$<TARGET_FILE_DIR:${PROJECT_NAME}>/licenses/LibRaw-COPYRIGHT.txt"
        VERBATIM
    )
endif()

# Link OpenGL based on platform
if (WIN32)
    target_link_libraries(${PROJECT_NAME} PRIVATE opengl32 comdlg32 dwmapi)
elseif (APPLE)
    target_link_libraries(${PROJECT_NAME} PRIVATE "-framework OpenGL")
elseif (UNIX)
    target_link_libraries(${PROJECT_NAME} PRIVATE GL)
endif()

option(STACK_BUILD_GRAPH_TESTS "Build focused node graph behavior tests" ON)
if (STACK_BUILD_GRAPH_TESTS)
    add_executable(StackGraphBehaviorTests
        tools/graph_behavior_tests.cpp
        src/Editor/NodeGraph/EditorNodeGraph.cpp
        src/Editor/NodeGraph/EditorNodeGraphDefinitions.cpp
        src/Editor/NodeGraph/Model/EditorNodeGraphLayoutValidation.cpp
        src/Editor/NodeGraph/Model/EditorNodeGraphMutation.cpp
        src/Editor/NodeGraph/Model/EditorNodeGraphSelection.cpp
        src/Editor/NodeGraph/Model/EditorNodeGraphTraversal.cpp
    )
    target_include_directories(StackGraphBehaviorTests PRIVATE
        src
        src/ThirdParty
    )
endif()
