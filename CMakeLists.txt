cmake_minimum_required(VERSION 3.20)
include(cmake/StackVersion.cmake)
project(Stack VERSION ${STACK_VERSION})

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/src/App/AppVersion.h.in"
    "${CMAKE_BINARY_DIR}/generated/src/App/AppVersion.h"
    @ONLY
)

if (WIN32)
    configure_file(
        "${CMAKE_CURRENT_SOURCE_DIR}/src/resources.rc.in"
        "${CMAKE_BINARY_DIR}/generated/src/resources.rc"
        @ONLY
    )
endif()

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
if (WIN32)
    option(STACK_ENABLE_APPWINDOW_TITLEBAR "Enable Windows App SDK AppWindowTitleBar bridge" ON)
else()
    option(STACK_ENABLE_APPWINDOW_TITLEBAR "Enable Windows App SDK AppWindowTitleBar bridge" OFF)
endif()
set(STACK_WINDOWS_APP_SDK_ROOT "" CACHE PATH "Optional root of a Microsoft.WindowsAppSDK NuGet package")

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

include(cmake/StackSources.cmake)

add_executable(${PROJECT_NAME} ${STACK_APP_SOURCE_FILES})
add_dependencies(${PROJECT_NAME} BakeAssets BakeTabIcons BakeFonts)

target_include_directories(${PROJECT_NAME} PRIVATE
    ${CMAKE_BINARY_DIR}/generated/src
    ${CMAKE_BINARY_DIR}/generated/src/App
    src
    src/ThirdParty
)

target_compile_definitions(${PROJECT_NAME} PRIVATE
    STACK_ENABLE_APPWINDOW_TITLEBAR=$<BOOL:${STACK_ENABLE_APPWINDOW_TITLEBAR}>
)

if (WIN32 AND STACK_ENABLE_APPWINDOW_TITLEBAR)
    set(STACK_WINDOWS_APP_SDK_CANDIDATE_ROOTS)
    if (STACK_WINDOWS_APP_SDK_ROOT)
        list(APPEND STACK_WINDOWS_APP_SDK_CANDIDATE_ROOTS "${STACK_WINDOWS_APP_SDK_ROOT}")
    endif()
    if (DEFINED ENV{STACK_WINDOWS_APP_SDK_ROOT} AND NOT "$ENV{STACK_WINDOWS_APP_SDK_ROOT}" STREQUAL "")
        list(APPEND STACK_WINDOWS_APP_SDK_CANDIDATE_ROOTS "$ENV{STACK_WINDOWS_APP_SDK_ROOT}")
    endif()
    if (DEFINED ENV{USERPROFILE})
        file(GLOB STACK_WINDOWS_APP_SDK_NUGET_ROOTS
            LIST_DIRECTORIES true
            "$ENV{USERPROFILE}/.nuget/packages/microsoft.windowsappsdk/*"
        )
        if (STACK_WINDOWS_APP_SDK_NUGET_ROOTS)
            list(SORT STACK_WINDOWS_APP_SDK_NUGET_ROOTS COMPARE NATURAL ORDER DESCENDING)
            list(APPEND STACK_WINDOWS_APP_SDK_CANDIDATE_ROOTS ${STACK_WINDOWS_APP_SDK_NUGET_ROOTS})
        endif()
    endif()
    file(GLOB STACK_WINDOWS_APP_SDK_LOCAL_PACKAGE_ROOTS
        LIST_DIRECTORIES true
        "${CMAKE_CURRENT_SOURCE_DIR}/_workspace/deps/Microsoft.WindowsAppSDK/*"
        "${CMAKE_CURRENT_SOURCE_DIR}/_workspace/deps/nuget/Microsoft.WindowsAppSDK.*/*"
    )
    if (STACK_WINDOWS_APP_SDK_LOCAL_PACKAGE_ROOTS)
        list(SORT STACK_WINDOWS_APP_SDK_LOCAL_PACKAGE_ROOTS COMPARE NATURAL ORDER DESCENDING)
        list(APPEND STACK_WINDOWS_APP_SDK_CANDIDATE_ROOTS ${STACK_WINDOWS_APP_SDK_LOCAL_PACKAGE_ROOTS})
    endif()
    set(STACK_WINDOWS_APP_SDK_GENERATED_CPPWINRT "${CMAKE_CURRENT_SOURCE_DIR}/_workspace/deps/generated/cppwinrt")

    set(STACK_HAS_WINDOWS_APP_SDK OFF)
    foreach(STACK_WINDOWS_APP_SDK_CANDIDATE IN LISTS STACK_WINDOWS_APP_SDK_CANDIDATE_ROOTS)
        if (STACK_HAS_WINDOWS_APP_SDK)
            break()
        endif()

        set(STACK_WINDOWS_APP_SDK_INCLUDE_SEARCH_DIRS)
        if (EXISTS "${STACK_WINDOWS_APP_SDK_GENERATED_CPPWINRT}/winrt/Microsoft.UI.Windowing.h")
            list(APPEND STACK_WINDOWS_APP_SDK_INCLUDE_SEARCH_DIRS "${STACK_WINDOWS_APP_SDK_GENERATED_CPPWINRT}")
        endif()
        foreach(STACK_WINDOWS_APP_SDK_INCLUDE_ROOT IN LISTS STACK_WINDOWS_APP_SDK_CANDIDATE_ROOTS)
            foreach(STACK_WINDOWS_APP_SDK_INCLUDE_DIR
                "${STACK_WINDOWS_APP_SDK_INCLUDE_ROOT}/include"
                "${STACK_WINDOWS_APP_SDK_INCLUDE_ROOT}/include/winrt"
                "${STACK_WINDOWS_APP_SDK_INCLUDE_ROOT}/build/native/include")
                if (EXISTS "${STACK_WINDOWS_APP_SDK_INCLUDE_DIR}")
                    list(APPEND STACK_WINDOWS_APP_SDK_INCLUDE_SEARCH_DIRS "${STACK_WINDOWS_APP_SDK_INCLUDE_DIR}")
                endif()
            endforeach()
        endforeach()
        list(REMOVE_DUPLICATES STACK_WINDOWS_APP_SDK_INCLUDE_SEARCH_DIRS)

        unset(STACK_WINDOWS_APP_SDK_WINRT_INCLUDE CACHE)
        unset(STACK_WINDOWS_APP_SDK_BOOTSTRAP_INCLUDE CACHE)
        unset(STACK_WINDOWS_APP_SDK_VERSION_INCLUDE CACHE)
        unset(STACK_WINDOWS_APP_SDK_BOOTSTRAP_LIB CACHE)
        unset(STACK_WINDOWS_APP_SDK_BOOTSTRAP_DLL CACHE)
        find_path(STACK_WINDOWS_APP_SDK_WINRT_INCLUDE
            NAMES
                winrt/Microsoft.UI.Windowing.h
                winrt/Microsoft.UI.Input.h
                winrt/Microsoft.UI.Interop.h
                winrt/Microsoft.ui.interop.h
            PATHS ${STACK_WINDOWS_APP_SDK_INCLUDE_SEARCH_DIRS}
            NO_DEFAULT_PATH
        )
        find_path(STACK_WINDOWS_APP_SDK_BOOTSTRAP_INCLUDE
            NAMES mddbootstrap.h MddBootstrap.h
            PATHS ${STACK_WINDOWS_APP_SDK_INCLUDE_SEARCH_DIRS}
            NO_DEFAULT_PATH
        )
        find_path(STACK_WINDOWS_APP_SDK_VERSION_INCLUDE
            NAMES WindowsAppSDK-VersionInfo.h
            PATHS ${STACK_WINDOWS_APP_SDK_INCLUDE_SEARCH_DIRS}
            NO_DEFAULT_PATH
        )
        find_library(STACK_WINDOWS_APP_SDK_BOOTSTRAP_LIB
            NAMES Microsoft.WindowsAppRuntime.Bootstrap
            PATHS
                "${STACK_WINDOWS_APP_SDK_CANDIDATE}/lib/native/x64"
                "${STACK_WINDOWS_APP_SDK_CANDIDATE}/lib/win10-x64"
                "${STACK_WINDOWS_APP_SDK_CANDIDATE}/lib/x64"
                "${STACK_WINDOWS_APP_SDK_CANDIDATE}/lib/native"
            NO_DEFAULT_PATH
        )
        find_file(STACK_WINDOWS_APP_SDK_BOOTSTRAP_DLL
            NAMES Microsoft.WindowsAppRuntime.Bootstrap.dll
            PATHS
                "${STACK_WINDOWS_APP_SDK_CANDIDATE}/runtimes/win-x64/native"
                "${STACK_WINDOWS_APP_SDK_CANDIDATE}/runtimes-framework/win-x64/native"
                "${STACK_WINDOWS_APP_SDK_CANDIDATE}/bin/x64"
            NO_DEFAULT_PATH
        )

        if (STACK_WINDOWS_APP_SDK_WINRT_INCLUDE AND
            STACK_WINDOWS_APP_SDK_BOOTSTRAP_INCLUDE AND
            STACK_WINDOWS_APP_SDK_VERSION_INCLUDE AND
            STACK_WINDOWS_APP_SDK_BOOTSTRAP_LIB AND
            STACK_WINDOWS_APP_SDK_BOOTSTRAP_DLL)
            set(STACK_HAS_WINDOWS_APP_SDK ON)
            set(STACK_WINDOWS_APP_SDK_ROOT_RESOLVED "${STACK_WINDOWS_APP_SDK_CANDIDATE}")
        endif()
    endforeach()

    file(GLOB STACK_WINDOWS_CPPWINRT_INCLUDE_CANDIDATES
        LIST_DIRECTORIES true
        "C:/Program Files (x86)/Windows Kits/10/Include/*/cppwinrt"
    )
    if (STACK_WINDOWS_CPPWINRT_INCLUDE_CANDIDATES)
        list(SORT STACK_WINDOWS_CPPWINRT_INCLUDE_CANDIDATES COMPARE NATURAL ORDER DESCENDING)
        list(GET STACK_WINDOWS_CPPWINRT_INCLUDE_CANDIDATES 0 STACK_WINDOWS_CPPWINRT_INCLUDE)
    endif()

    if (STACK_HAS_WINDOWS_APP_SDK)
        message(STATUS "Windows App SDK titlebar bridge enabled from: ${STACK_WINDOWS_APP_SDK_ROOT_RESOLVED}")
        target_compile_definitions(${PROJECT_NAME} PRIVATE STACK_HAS_WINDOWS_APP_SDK=1)
        target_include_directories(${PROJECT_NAME} PRIVATE
            "${STACK_WINDOWS_APP_SDK_WINRT_INCLUDE}"
            "${STACK_WINDOWS_APP_SDK_BOOTSTRAP_INCLUDE}"
            "${STACK_WINDOWS_APP_SDK_VERSION_INCLUDE}"
            ${STACK_WINDOWS_APP_SDK_INCLUDE_SEARCH_DIRS}
        )
        if (STACK_WINDOWS_CPPWINRT_INCLUDE)
            target_include_directories(${PROJECT_NAME} PRIVATE "${STACK_WINDOWS_CPPWINRT_INCLUDE}")
        endif()
        target_link_libraries(${PROJECT_NAME} PRIVATE
            "${STACK_WINDOWS_APP_SDK_BOOTSTRAP_LIB}"
            runtimeobject
        )
        add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${STACK_WINDOWS_APP_SDK_BOOTSTRAP_DLL}"
                "$<TARGET_FILE_DIR:${PROJECT_NAME}>"
            VERBATIM
        )
    else()
        message(WARNING "STACK_ENABLE_APPWINDOW_TITLEBAR=ON, but Microsoft.WindowsAppSDK was not found. Stack will build with the native titlebar fallback.")
        target_compile_definitions(${PROJECT_NAME} PRIVATE STACK_HAS_WINDOWS_APP_SDK=0)
    endif()
else()
    target_compile_definitions(${PROJECT_NAME} PRIVATE STACK_HAS_WINDOWS_APP_SDK=0)
endif()

target_link_libraries(${PROJECT_NAME} PRIVATE 
    glfw 
    imgui_core
)

if (STACK_ENABLE_LIBRAW)
    target_compile_definitions(${PROJECT_NAME} PRIVATE STACK_ENABLE_LIBRAW=1)
    target_link_libraries(${PROJECT_NAME} PRIVATE stack_libraw)
    if (WIN32 AND MSVC)
        target_link_options(${PROJECT_NAME} PRIVATE "/DELAYLOAD:libraw.dll")
        target_link_libraries(${PROJECT_NAME} PRIVATE delayimp)
    endif()
    add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_CURRENT_SOURCE_DIR}/THIRD_PARTY_NOTICES.md"
            "$<TARGET_FILE_DIR:${PROJECT_NAME}>/THIRD_PARTY_NOTICES.md"
        COMMAND ${CMAKE_COMMAND} -E remove_directory
            "$<TARGET_FILE_DIR:${PROJECT_NAME}>/licenses"
        COMMAND ${CMAKE_COMMAND} -E make_directory
            "$<TARGET_FILE_DIR:${PROJECT_NAME}>/licenses"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${imgui_SOURCE_DIR}/LICENSE.txt"
            "$<TARGET_FILE_DIR:${PROJECT_NAME}>/licenses/DearImGui-MIT.txt"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${glfw_SOURCE_DIR}/LICENSE.md"
            "$<TARGET_FILE_DIR:${PROJECT_NAME}>/licenses/GLFW-zlib-libpng.txt"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${libraw_SOURCE_DIR}/LICENSE.LGPL"
            "$<TARGET_FILE_DIR:${PROJECT_NAME}>/licenses/LibRaw-LGPL-2.1.txt"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${libraw_SOURCE_DIR}/COPYRIGHT"
            "$<TARGET_FILE_DIR:${PROJECT_NAME}>/licenses/LibRaw-COPYRIGHT.txt"
        VERBATIM
    )
endif()

# Link OpenGL based on platform
if (WIN32)
    target_link_libraries(${PROJECT_NAME} PRIVATE opengl32 comdlg32 dwmapi shell32 ole32 shlwapi winhttp bcrypt)
elseif (APPLE)
    target_link_libraries(${PROJECT_NAME} PRIVATE "-framework OpenGL")
elseif (UNIX)
    target_link_libraries(${PROJECT_NAME} PRIVATE GL)
endif()

if (WIN32)
    set(STACK_BUILDER_DIR "${CMAKE_CURRENT_SOURCE_DIR}/_workspace/stack_builder")
    if (EXISTS "${STACK_BUILDER_DIR}/CMakeLists.txt")
        add_subdirectory("${STACK_BUILDER_DIR}" "${CMAKE_BINARY_DIR}/stack_builder")
    endif()
endif()

option(STACK_BUILD_GRAPH_TESTS "Build focused node graph behavior tests" ON)
if (STACK_BUILD_GRAPH_TESTS)
    add_executable(StackGraphBehaviorTests ${STACK_GRAPH_BEHAVIOR_TEST_SOURCE_FILES})
    target_include_directories(StackGraphBehaviorTests PRIVATE
        src
        src/ThirdParty
    )
    if (STACK_ENABLE_LIBRAW)
        target_compile_definitions(StackGraphBehaviorTests PRIVATE STACK_ENABLE_LIBRAW=1)
        target_link_libraries(StackGraphBehaviorTests PRIVATE stack_libraw)
        if (WIN32 AND MSVC)
            target_link_options(StackGraphBehaviorTests PRIVATE "/DELAYLOAD:libraw.dll")
            target_link_libraries(StackGraphBehaviorTests PRIVATE delayimp)
        endif()
    endif()
endif()
