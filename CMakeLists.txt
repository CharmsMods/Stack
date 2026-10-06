cmake_minimum_required(VERSION 3.20)
include(cmake/StackVersion.cmake)
project(Stack VERSION ${STACK_VERSION})

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(STACK_LEGAL_STATUS_FILE "${CMAKE_CURRENT_SOURCE_DIR}/legal/RELEASE_LEGAL_STATUS.json")
file(READ "${STACK_LEGAL_STATUS_FILE}" STACK_LEGAL_STATUS_JSON)
string(JSON STACK_EULA_VERSION GET "${STACK_LEGAL_STATUS_JSON}" documents eula version)
string(JSON STACK_PRIVACY_VERSION GET "${STACK_LEGAL_STATUS_JSON}" documents privacy version)
file(SHA256 "${CMAKE_CURRENT_SOURCE_DIR}/legal/EULA.txt" STACK_EULA_SHA256)

configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/src/App/AppVersion.h.in"
    "${CMAKE_BINARY_DIR}/generated/src/App/AppVersion.h"
    @ONLY
)

configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/src/App/AppLegalVersion.h.in"
    "${CMAKE_BINARY_DIR}/generated/src/App/AppLegalVersion.h"
    @ONLY
)

if (WIN32)
    configure_file(
        "${CMAKE_CURRENT_SOURCE_DIR}/src/resources.rc.in"
        "${CMAKE_BINARY_DIR}/generated/src/resources.rc"
        @ONLY
    )
endif()

# CMake owns a private binary tree. Only the runnable app target and its shipped
# payload are written to STACK_DEPLOY_DIR.
set(STACK_DEPLOY_DIR "${CMAKE_CURRENT_SOURCE_DIR}/build" CACHE PATH "Runnable Stack deployment directory")
get_filename_component(STACK_DEPLOY_DIR "${STACK_DEPLOY_DIR}" ABSOLUTE BASE_DIR "${CMAKE_CURRENT_SOURCE_DIR}")
set(STACK_PRIVATE_RUNTIME_DIR "${CMAKE_BINARY_DIR}/artifacts")
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${STACK_PRIVATE_RUNTIME_DIR}")
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_RELEASE "${STACK_PRIVATE_RUNTIME_DIR}")
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_DEBUG "${STACK_PRIVATE_RUNTIME_DIR}")
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
option(STACK_ENABLE_RESTORMER_EXECUTION "Enable private Restormer package probing and model-service execution" OFF)
if (WIN32)
    option(STACK_ENABLE_APPWINDOW_TITLEBAR "Enable Windows App SDK AppWindowTitleBar bridge" ON)
else()
    option(STACK_ENABLE_APPWINDOW_TITLEBAR "Enable Windows App SDK AppWindowTitleBar bridge" OFF)
endif()
set(STACK_WINDOWS_APP_SDK_ROOT "" CACHE PATH "Optional root of a Microsoft.WindowsAppSDK NuGet package")
set(STACK_WINDOWS_ML_ROOT "" CACHE PATH "Optional root of the Microsoft.Windows.AI.MachineLearning NuGet package")

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
    GIT_TAG 5a76f2adf1b0403b86a45010121fb32a6bff8680
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
        RUNTIME_OUTPUT_DIRECTORY "${STACK_PRIVATE_RUNTIME_DIR}"
        RUNTIME_OUTPUT_DIRECTORY_RELEASE "${STACK_PRIVATE_RUNTIME_DIR}"
        RUNTIME_OUTPUT_DIRECTORY_DEBUG "${STACK_PRIVATE_RUNTIME_DIR}"
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
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tools/assets/bake_splash.py"
    WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
    COMMENT "Baking assets into C++..."
    VERBATIM
)

add_custom_target(BakeTabIcons
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tools/assets/bake_tab_icons.py"
    WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
    COMMENT "Baking tab icons into C++..."
    VERBATIM
)

add_custom_target(BakeFonts
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tools/assets/bake_fonts.py"
    WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
    COMMENT "Baking fonts into C++..."
    VERBATIM
)

include(cmake/StackSources.cmake)

if (WIN32)
    add_executable(${PROJECT_NAME} WIN32 ${STACK_APP_SOURCE_FILES})
else()
    add_executable(${PROJECT_NAME} ${STACK_APP_SOURCE_FILES})
endif()
add_dependencies(${PROJECT_NAME} BakeAssets BakeTabIcons BakeFonts)
include(cmake/StackPanorama.cmake)
set_target_properties(${PROJECT_NAME} PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${STACK_DEPLOY_DIR}"
    RUNTIME_OUTPUT_DIRECTORY_RELEASE "${STACK_DEPLOY_DIR}"
    RUNTIME_OUTPUT_DIRECTORY_DEBUG "${STACK_DEPLOY_DIR}"
)

if (MSVC)
    # RAW graph execution combines nested graph evaluation, stage caching, and
    # validation/readback paths. Keep modest headroom above the Windows 1 MiB
    # default without changing runtime allocation behavior; committed stack
    # pages are still demand-backed.
    target_link_options(${PROJECT_NAME} PRIVATE "/STACK:2097152")
    set(STACK_MSVC_COMPILE_JOBS 14 CACHE STRING "Maximum parallel Stack C++ compilations")
    if (STACK_MSVC_COMPILE_JOBS GREATER 1)
        target_compile_options(${PROJECT_NAME} PRIVATE "/MP${STACK_MSVC_COMPILE_JOBS}")
    endif()
endif()

target_include_directories(${PROJECT_NAME} PRIVATE
    ${CMAKE_BINARY_DIR}/generated/src
    ${CMAKE_BINARY_DIR}/generated/src/App
    src
    src/ThirdParty
)

target_compile_definitions(${PROJECT_NAME} PRIVATE
    STACK_ENABLE_APPWINDOW_TITLEBAR=$<BOOL:${STACK_ENABLE_APPWINDOW_TITLEBAR}>
    STACK_ENABLE_RESTORMER_EXECUTION=$<BOOL:${STACK_ENABLE_RESTORMER_EXECUTION}>
    STACK_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}"
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
        # The checked-in Microsoft.UI projection can lag the servicing revision
        # of the Windows SDK's C++/WinRT base header. Normalize a private copy so
        # one compilation never mixes two CPPWINRT_VERSION values.
        if (STACK_WINDOWS_CPPWINRT_INCLUDE AND
            EXISTS "${STACK_WINDOWS_APP_SDK_GENERATED_CPPWINRT}/winrt/Microsoft.UI.h")
            file(READ
                "${STACK_WINDOWS_CPPWINRT_INCLUDE}/winrt/base.h"
                STACK_CPPWINRT_BASE_HEADER)
            string(REGEX MATCH
                "CPPWINRT_VERSION[ \t]+\"([^\"]+)\""
                STACK_CPPWINRT_VERSION_MATCH
                "${STACK_CPPWINRT_BASE_HEADER}")
            set(STACK_CPPWINRT_SDK_VERSION "${CMAKE_MATCH_1}")
            file(READ
                "${STACK_WINDOWS_APP_SDK_GENERATED_CPPWINRT}/winrt/Microsoft.UI.h"
                STACK_CPPWINRT_APP_SDK_HEADER)
            string(REGEX MATCH
                "C[+][+]/WinRT v([^\r\n]+)"
                STACK_CPPWINRT_APP_SDK_VERSION_MATCH
                "${STACK_CPPWINRT_APP_SDK_HEADER}")
            set(STACK_CPPWINRT_APP_SDK_VERSION "${CMAKE_MATCH_1}")
            if (STACK_CPPWINRT_SDK_VERSION AND STACK_CPPWINRT_APP_SDK_VERSION)
                set(STACK_NORMALIZED_CPPWINRT "${CMAKE_BINARY_DIR}/generated/cppwinrt")
                file(REMOVE_RECURSE "${STACK_NORMALIZED_CPPWINRT}/winrt")
                file(COPY
                    "${STACK_WINDOWS_APP_SDK_GENERATED_CPPWINRT}/winrt"
                    DESTINATION "${STACK_NORMALIZED_CPPWINRT}")
                file(GLOB_RECURSE STACK_NORMALIZED_CPPWINRT_HEADERS
                    "${STACK_NORMALIZED_CPPWINRT}/winrt/*.h")
                foreach(STACK_NORMALIZED_HEADER IN LISTS STACK_NORMALIZED_CPPWINRT_HEADERS)
                    file(READ "${STACK_NORMALIZED_HEADER}" STACK_NORMALIZED_HEADER_CONTENT)
                    string(REPLACE
                        "${STACK_CPPWINRT_APP_SDK_VERSION}"
                        "${STACK_CPPWINRT_SDK_VERSION}"
                        STACK_NORMALIZED_HEADER_CONTENT
                        "${STACK_NORMALIZED_HEADER_CONTENT}")
                    file(WRITE "${STACK_NORMALIZED_HEADER}" "${STACK_NORMALIZED_HEADER_CONTENT}")
                endforeach()
                set(STACK_WINDOWS_APP_SDK_WINRT_INCLUDE "${STACK_NORMALIZED_CPPWINRT}")
            endif()
        endif()
        target_compile_definitions(${PROJECT_NAME} PRIVATE STACK_HAS_WINDOWS_APP_SDK=1)
        if (MSVC)
            target_compile_definitions(${PROJECT_NAME} PRIVATE
                _SILENCE_EXPERIMENTAL_COROUTINE_DEPRECATION_WARNINGS)
        endif()
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
            runtimeobject
        )
        add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E make_directory
                "${STACK_DEPLOY_DIR}/Stack/App/Runtime"
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${STACK_WINDOWS_APP_SDK_BOOTSTRAP_DLL}"
                "${STACK_DEPLOY_DIR}/Stack/App/Runtime/Microsoft.WindowsAppRuntime.Bootstrap.dll"
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

if (WIN32)
    target_link_libraries(${PROJECT_NAME} PRIVATE shell32)
endif()

add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory
        "${STACK_DEPLOY_DIR}/Stack/App/Runtime"
    COMMAND ${CMAKE_COMMAND} -E rm -f
        "${STACK_DEPLOY_DIR}/Stack/App/Runtime/libraw.dll"
    COMMAND ${CMAKE_COMMAND} -E make_directory
        "${STACK_DEPLOY_DIR}/Stack/App/Resources"
    COMMAND ${CMAKE_COMMAND} -E make_directory
        "${STACK_DEPLOY_DIR}/Stack/App/Tools"
    COMMAND ${CMAKE_COMMAND} -E make_directory
        "${STACK_DEPLOY_DIR}/Stack/App/Legal"
    COMMAND ${CMAKE_COMMAND} -E remove_directory
        "${STACK_DEPLOY_DIR}/Stack/App/Legal/ThirdParty"
    COMMAND ${CMAKE_COMMAND} -E make_directory
        "${STACK_DEPLOY_DIR}/Stack/App/Legal/ThirdParty"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_CURRENT_SOURCE_DIR}/legal/EULA.txt"
        "${STACK_DEPLOY_DIR}/Stack/App/Legal/EULA.txt"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_CURRENT_SOURCE_DIR}/legal/PRIVACY.md"
        "${STACK_DEPLOY_DIR}/Stack/App/Legal/PRIVACY.md"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_CURRENT_SOURCE_DIR}/legal/INFO_BEFORE_INSTALL.txt"
        "${STACK_DEPLOY_DIR}/Stack/App/Legal/INFO_BEFORE_INSTALL.txt"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_CURRENT_SOURCE_DIR}/THIRD_PARTY_NOTICES.md"
        "${STACK_DEPLOY_DIR}/Stack/App/Legal/THIRD_PARTY_NOTICES.md"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_CURRENT_SOURCE_DIR}/LICENSE"
        "${STACK_DEPLOY_DIR}/Stack/App/Legal/LICENSE"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${imgui_SOURCE_DIR}/LICENSE.txt"
        "${STACK_DEPLOY_DIR}/Stack/App/Legal/ThirdParty/DearImGui-MIT.txt"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${glfw_SOURCE_DIR}/LICENSE.md"
        "${STACK_DEPLOY_DIR}/Stack/App/Legal/ThirdParty/GLFW-zlib-libpng.txt"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_CURRENT_SOURCE_DIR}/legal/third_party/NlohmannJSON-MIT.txt"
        "${STACK_DEPLOY_DIR}/Stack/App/Legal/ThirdParty/NlohmannJSON-MIT.txt"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_CURRENT_SOURCE_DIR}/legal/third_party/stb-MIT-or-Public-Domain.txt"
        "${STACK_DEPLOY_DIR}/Stack/App/Legal/ThirdParty/stb-MIT-or-Public-Domain.txt"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_CURRENT_SOURCE_DIR}/legal/third_party/OpenCV-Apache-2.0.txt"
        "${CMAKE_CURRENT_SOURCE_DIR}/legal/third_party/OpenCV-COPYRIGHT.txt"
        "${CMAKE_CURRENT_SOURCE_DIR}/legal/third_party/OpenCV-zlib.txt"
        "${CMAKE_CURRENT_SOURCE_DIR}/legal/third_party/OpenCV-SoftFloat-FDLIBM.txt"
        "${CMAKE_CURRENT_SOURCE_DIR}/legal/third_party/OpenCV-FLANN-BSD.txt"
        "${STACK_DEPLOY_DIR}/Stack/App/Legal/ThirdParty"
    VERBATIM
)

if (STACK_ENABLE_LIBRAW)
    target_compile_definitions(${PROJECT_NAME} PRIVATE STACK_ENABLE_LIBRAW=1)
    target_link_libraries(${PROJECT_NAME} PRIVATE stack_libraw)
    if (WIN32 AND MSVC)
        target_link_options(${PROJECT_NAME} PRIVATE "/DELAYLOAD:libraw.dll")
        target_link_libraries(${PROJECT_NAME} PRIVATE delayimp)
    endif()
    add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory
            "${STACK_DEPLOY_DIR}/Stack/App/Runtime"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "$<TARGET_FILE:stack_libraw>"
            "${STACK_DEPLOY_DIR}/Stack/App/Runtime/libraw.dll"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${libraw_SOURCE_DIR}/LICENSE.LGPL"
            "${STACK_DEPLOY_DIR}/Stack/App/Legal/ThirdParty/LibRaw-LGPL-2.1.txt"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${libraw_SOURCE_DIR}/COPYRIGHT"
            "${STACK_DEPLOY_DIR}/Stack/App/Legal/ThirdParty/LibRaw-COPYRIGHT.txt"
        VERBATIM
    )
endif()

if (STACK_HAS_WINDOWS_APP_SDK AND
    EXISTS "${STACK_WINDOWS_APP_SDK_ROOT_RESOLVED}/license.txt")
    add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${STACK_WINDOWS_APP_SDK_ROOT_RESOLVED}/license.txt"
            "${STACK_DEPLOY_DIR}/Stack/App/Legal/ThirdParty/Microsoft-WindowsAppSDK-License.txt"
        VERBATIM
    )
endif()

if (NOT STACK_HAS_WINDOWS_APP_SDK)
    add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E rm -f
            "${STACK_DEPLOY_DIR}/Stack/App/Runtime/Microsoft.WindowsAppRuntime.Bootstrap.dll"
        VERBATIM
    )
endif()

# Link OpenGL based on platform
if (WIN32)
    target_link_libraries(${PROJECT_NAME} PRIVATE opengl32 comdlg32 comctl32 dwmapi shell32 ole32 shlwapi winhttp bcrypt)
elseif (APPLE)
    target_link_libraries(${PROJECT_NAME} PRIVATE "-framework OpenGL")
elseif (UNIX)
    target_link_libraries(${PROJECT_NAME} PRIVATE GL)
endif()

option(STACK_BUILD_MODEL_SERVICE "Build the optional external Windows ML model service" OFF)

if (WIN32 AND STACK_BUILD_MODEL_SERVICE)
    set(STACK_WINDOWS_ML_CANDIDATE_ROOTS)
    if (STACK_WINDOWS_ML_ROOT)
        list(APPEND STACK_WINDOWS_ML_CANDIDATE_ROOTS "${STACK_WINDOWS_ML_ROOT}")
    endif()
    if (DEFINED ENV{STACK_WINDOWS_ML_ROOT} AND NOT "$ENV{STACK_WINDOWS_ML_ROOT}" STREQUAL "")
        list(APPEND STACK_WINDOWS_ML_CANDIDATE_ROOTS "$ENV{STACK_WINDOWS_ML_ROOT}")
    endif()
    file(GLOB STACK_WINDOWS_ML_LOCAL_ROOTS
        LIST_DIRECTORIES true
        "${CMAKE_CURRENT_SOURCE_DIR}/_workspace/deps/nuget/Microsoft.Windows.AI.MachineLearning/*"
    )
    if (STACK_WINDOWS_ML_LOCAL_ROOTS)
        list(SORT STACK_WINDOWS_ML_LOCAL_ROOTS COMPARE NATURAL ORDER DESCENDING)
        list(APPEND STACK_WINDOWS_ML_CANDIDATE_ROOTS ${STACK_WINDOWS_ML_LOCAL_ROOTS})
    endif()

    set(STACK_WINDOWS_ML_FOUND OFF)
    foreach(STACK_WINDOWS_ML_CANDIDATE IN LISTS STACK_WINDOWS_ML_CANDIDATE_ROOTS)
        if (EXISTS "${STACK_WINDOWS_ML_CANDIDATE}/build/cmake/microsoft.windows.ai.machinelearning-config.cmake")
            find_package(
                microsoft.windows.ai.machinelearning CONFIG QUIET
                PATHS "${STACK_WINDOWS_ML_CANDIDATE}/build/cmake"
                NO_DEFAULT_PATH
            )
            if (TARGET WindowsML::WindowsML)
                set(STACK_WINDOWS_ML_FOUND ON)
                set(STACK_WINDOWS_ML_ROOT_RESOLVED "${STACK_WINDOWS_ML_CANDIDATE}")
                break()
            endif()
        endif()
    endforeach()

    if (STACK_WINDOWS_ML_FOUND)
        message(STATUS "External StackModelService enabled from: ${STACK_WINDOWS_ML_ROOT_RESOLVED}")
        add_executable(StackModelService
            "${CMAKE_CURRENT_SOURCE_DIR}/tools/restormer/StackModelService.cpp"
            "${CMAKE_CURRENT_SOURCE_DIR}/src/Restormer/RestormerProtocol.cpp"
            "${CMAKE_CURRENT_SOURCE_DIR}/src/Restormer/RestormerTiling.cpp"
        )
        add_executable(StackModelServiceProtocolTests
            "${CMAKE_CURRENT_SOURCE_DIR}/tools/restormer/StackModelServiceProtocolTests.cpp"
            "${CMAKE_CURRENT_SOURCE_DIR}/src/Restormer/RestormerProtocol.cpp"
        )
        set_target_properties(StackModelService PROPERTIES
            CXX_STANDARD 20
            CXX_STANDARD_REQUIRED ON
            CXX_EXTENSIONS OFF
        )
        set_target_properties(StackModelServiceProtocolTests PROPERTIES
            CXX_STANDARD 17
            CXX_STANDARD_REQUIRED ON
            CXX_EXTENSIONS OFF
        )
        target_include_directories(StackModelService PRIVATE
            src
            src/ThirdParty
        )
        target_include_directories(StackModelServiceProtocolTests PRIVATE
            src
            src/ThirdParty
        )
        target_link_libraries(StackModelService PRIVATE
            WindowsML::WindowsML
        )
        add_custom_command(TARGET StackModelService POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                $<TARGET_RUNTIME_DLLS:StackModelService>
                $<TARGET_FILE_DIR:StackModelService>
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${WINML_DIRECTML_DLL}"
                "$<TARGET_FILE_DIR:StackModelService>"
            COMMAND_EXPAND_LISTS
            VERBATIM
        )
    else()
        message(WARNING "Microsoft.Windows.AI.MachineLearning was not found; StackModelService will not be built.")
    endif()
endif()

option(STACK_BUILD_AUXILIARY_TOOLS "Build obsolete/private auxiliary Stack tools" OFF)
if (WIN32 AND STACK_BUILD_AUXILIARY_TOOLS)
    set(STACK_BUILDER_DIR "${CMAKE_CURRENT_SOURCE_DIR}/_workspace/stack_builder")
    if (EXISTS "${STACK_BUILDER_DIR}/CMakeLists.txt")
        add_subdirectory("${STACK_BUILDER_DIR}" "${CMAKE_BINARY_DIR}/stack_builder")
    endif()
endif()

include(CTest)
if(BUILD_TESTING)
    include(cmake/StackGpuUploadTests.cmake)
    include(cmake/StackZoneAreaTests.cmake)
    include(cmake/StackGraphControlTests.cmake)
    include(cmake/StackColorOrientationTests.cmake)
    include(cmake/StackCreamPaletteTests.cmake)
endif()

if (BUILD_TESTING)
    include(cmake/StackWorkspaceSwitcherTests.cmake)
    add_executable(StackRawGallerySimilarityTests
        tools/tests/raw_gallery_similarity_tests.cpp
        src/Raw/RawGalleryFilmstripOrganization.cpp
        src/Raw/RawGallerySimilarity.cpp)
    target_include_directories(StackRawGallerySimilarityTests PRIVATE src)
    add_test(NAME StackRaw.GallerySimilarity COMMAND StackRawGallerySimilarityTests)
    set_tests_properties(StackRaw.GallerySimilarity PROPERTIES
        LABELS "raw;gallery;similarity;cpu")

    add_executable(StackStartupRevealTests
        tools/tests/startup_reveal_tests.cpp
        src/App/StartupReveal.cpp)
    target_include_directories(StackStartupRevealTests PRIVATE src)
    target_link_libraries(StackStartupRevealTests PRIVATE imgui_core)
    add_test(NAME StackApp.StartupReveal COMMAND StackStartupRevealTests)
    set_tests_properties(StackApp.StartupReveal PROPERTIES LABELS "app;startup;cpu")

    add_test(NAME StackRaw.GradingScopes
        COMMAND $<TARGET_FILE:${PROJECT_NAME}> --validate-raw-grading-scopes)
    set_tests_properties(StackRaw.GradingScopes PROPERTIES LABELS "raw;scopes;gpu")
    add_executable(StackUiActivityTests tools/tests/ui_activity_tests.cpp
        src/Async/TaskSystem.cpp
        src/Notifications/NotificationStore.cpp src/Notifications/Notifier.cpp
        src/Notifications/NotificationIndicator.cpp src/Notifications/NotificationPresentation.cpp
        src/Notifications/AnimatedIndicatorLabel.cpp src/ThirdParty/imstb_truetype_impl.cpp)
    target_include_directories(StackUiActivityTests PRIVATE src)
    target_link_libraries(StackUiActivityTests PRIVATE imgui_core)
    add_test(NAME StackUi.Activity COMMAND StackUiActivityTests)
    set_tests_properties(StackUi.Activity PROPERTIES LABELS "ui;notifications;async;cpu" TIMEOUT 30)

    add_executable(StackAsyncReliabilityTests
        tools/tests/async_reliability_tests.cpp
        src/Async/TaskSystem.cpp)
    target_include_directories(StackAsyncReliabilityTests PRIVATE src)
    add_test(NAME StackAsync.Reliability COMMAND StackAsyncReliabilityTests)
    set_tests_properties(StackAsync.Reliability PROPERTIES
        LABELS "async;reliability;cpu" TIMEOUT 30)

    if (WIN32)
        add_executable(StackComApartmentTests tools/tests/com_apartment_tests.cpp)
        target_include_directories(StackComApartmentTests PRIVATE src)
        target_link_libraries(StackComApartmentTests PRIVATE ole32)
        add_test(NAME StackPlatform.ComApartment COMMAND StackComApartmentTests)
        set_tests_properties(StackPlatform.ComApartment PROPERTIES
            LABELS "platform;dialogs;cpu" TIMEOUT 30)
    endif()
endif()

if (BUILD_TESTING)
    add_executable(StackAppPathsTests
        "${CMAKE_CURRENT_SOURCE_DIR}/tools/tests/app_paths_tests.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/App/AppPaths.cpp"
    )
    target_include_directories(StackAppPathsTests PRIVATE src src/ThirdParty)
    if (WIN32)
        target_link_libraries(StackAppPathsTests PRIVATE shell32 ole32)
    endif()
    add_test(NAME StackApp.RuntimePaths COMMAND StackAppPathsTests)
    set_tests_properties(StackApp.RuntimePaths PROPERTIES LABELS "app;paths;migration;cpu")

    add_executable(StackUpdateReliabilityTests
        tools/tests/update_reliability_tests.cpp src/App/UpdateDownloadStorage.cpp)
    target_include_directories(StackUpdateReliabilityTests PRIVATE src)
    add_test(NAME StackApp.UpdateReliability COMMAND StackUpdateReliabilityTests)
    set_tests_properties(StackApp.UpdateReliability PROPERTIES LABELS "app;updates;cpu")

    add_executable(StackSavePathConfirmationTests tools/tests/save_path_confirmation_tests.cpp)
    target_include_directories(StackSavePathConfirmationTests PRIVATE src)
    add_test(NAME StackPlatform.SavePathConfirmation COMMAND StackSavePathConfirmationTests)
    set_tests_properties(StackPlatform.SavePathConfirmation PROPERTIES
        LABELS "platform;dialogs;cpu")

    add_test(
        NAME StackApp.AppearanceSettings
        COMMAND $<TARGET_FILE:${PROJECT_NAME}> --validate-appearance-settings
    )
    set_tests_properties(StackApp.AppearanceSettings PROPERTIES
        LABELS "app;appearance;settings;migration;cpu"
    )
endif()

if (BUILD_TESTING)
    add_test(
        NAME StackRaw.MfdPhase0
        COMMAND $<TARGET_FILE:${PROJECT_NAME}> --validate-mfd-phase0
    )
    set_tests_properties(StackRaw.MfdPhase0 PROPERTIES
        LABELS "raw;mfd;contract;cpu"
    )

    add_test(
        NAME StackRaw.MfdPhase1
        COMMAND $<TARGET_FILE:${PROJECT_NAME}> --validate-mfd-phase1
    )
    set_tests_properties(StackRaw.MfdPhase1 PROPERTIES
        LABELS "raw;mfd;preparation;cache;cpu"
    )

    add_test(
        NAME StackRaw.MfdPhase2
        COMMAND $<TARGET_FILE:${PROJECT_NAME}> --validate-mfd-phase2
    )
    set_tests_properties(StackRaw.MfdPhase2 PROPERTIES
        LABELS "raw;mfd;noise;variance;cpu"
    )

    add_test(
        NAME StackRaw.MfdPhase3
        COMMAND $<TARGET_FILE:${PROJECT_NAME}> --validate-mfd-phase3
    )
    set_tests_properties(StackRaw.MfdPhase3 PROPERTIES
        LABELS "raw;mfd;sampler;cfa;variance;cpu"
    )

    add_test(
        NAME StackRaw.MfdPhase4
        COMMAND $<TARGET_FILE:${PROJECT_NAME}> --validate-mfd-phase4
    )
    set_tests_properties(StackRaw.MfdPhase4 PROPERTIES
        LABELS "raw;mfd;registration;exposure;affine;fft;cpu"
    )

    add_test(
        NAME StackRaw.MfdPhase5
        COMMAND $<TARGET_FILE:${PROJECT_NAME}> --validate-mfd-phase5
    )
    set_tests_properties(StackRaw.MfdPhase5 PROPERTIES
        LABELS "raw;mfd;registration;motion;covariance;pyramid;cpu"
    )

    add_test(
        NAME StackRaw.MfdPhase6
        COMMAND $<TARGET_FILE:${PROJECT_NAME}> --validate-mfd-phase6
    )
    set_tests_properties(StackRaw.MfdPhase6 PROPERTIES
        LABELS "raw;mfd;reliability;motion;noise;gating;cache;cpu"
    )

    add_test(
        NAME StackRaw.MfdPhase7
        COMMAND $<TARGET_FILE:${PROJECT_NAME}> --validate-mfd-phase7
    )
    set_tests_properties(StackRaw.MfdPhase7 PROPERTIES
        LABELS "raw;mfd;fusion;variance;reliability;gating;diagnostics;cpu"
    )

    add_test(
        NAME StackRaw.MfdPhase8
        COMMAND $<TARGET_FILE:${PROJECT_NAME}> --validate-mfd-phase8
    )
    set_tests_properties(StackRaw.MfdPhase8 PROPERTIES
        LABELS "raw;mfd;streaming;cache;cancellation;determinism;memory;cpu"
    )

    add_test(
        NAME StackRaw.MfdPhase9
        COMMAND $<TARGET_FILE:${PROJECT_NAME}> --validate-mfd-phase9
    )
    set_tests_properties(StackRaw.MfdPhase9 PROPERTIES
        LABELS "raw;mfd;corpus;metrics;ablation;calibration;release-gate;cpu"
    )

    add_test(
        NAME StackRaw.MfdCorpusIntake
        COMMAND $<TARGET_FILE:${PROJECT_NAME}> --validate-mfd-corpus-intake
    )
    set_tests_properties(StackRaw.MfdCorpusIntake PROPERTIES
        LABELS "raw;mfd;corpus;intake;manifest;compatibility;cpu"
    )

    add_test(
        NAME StackRaw.MfdEndToEnd
        COMMAND $<TARGET_FILE:${PROJECT_NAME}> --validate-mfd-end-to-end
    )
    set_tests_properties(StackRaw.MfdEndToEnd PROPERTIES
        LABELS "raw;mfd;processor;registration;fusion;atomicity;memory;cpu"
    )

    add_test(NAME StackRaw.HdrContracts
        COMMAND $<TARGET_FILE:${PROJECT_NAME}> --validate-hdr-contracts)
    set_tests_properties(StackRaw.HdrContracts PROPERTIES
        LABELS "raw;hdr;contract;overrange;cache;cpu")

    add_test(NAME StackRaw.HdrCalibration
        COMMAND $<TARGET_FILE:${PROJECT_NAME}> --validate-hdr-calibration)
    set_tests_properties(StackRaw.HdrCalibration PROPERTIES
        LABELS "raw;hdr;calibration;mixed-iso;noise;cpu")

    add_test(NAME StackRaw.HdrTranslation
        COMMAND $<TARGET_FILE:${PROJECT_NAME}> --validate-hdr-translation)
    set_tests_properties(StackRaw.HdrTranslation PROPERTIES
        LABELS "raw;hdr;registration;fusion;cfa;cpu")

    add_test(NAME StackRaw.HdrProjectFoundation
        COMMAND $<TARGET_FILE:${PROJECT_NAME}> --validate-hdr-project-foundation)
    set_tests_properties(StackRaw.HdrProjectFoundation PROPERTIES
        LABELS "raw;hdr;project;migration;compatibility;cpu")

    add_test(NAME StackRaw.HdrEndToEnd
        COMMAND $<TARGET_FILE:${PROJECT_NAME}> --validate-hdr-end-to-end)
    set_tests_properties(StackRaw.HdrEndToEnd PROPERTIES
        LABELS "raw;hdr;processor;all-cfa;determinism;cpu")

    add_test(NAME StackRaw.ManualHdrFusion
        COMMAND $<TARGET_FILE:${PROJECT_NAME}> --validate-manual-hdr-fusion)
    set_tests_properties(StackRaw.ManualHdrFusion PROPERTIES
        LABELS "raw;hdr;manual;curves;masks;cache;graph;cpu")

    add_test(NAME StackRaw.HdrGpu
        COMMAND $<TARGET_FILE:${PROJECT_NAME}> --validate-hdr-gpu)
    set_tests_properties(StackRaw.HdrGpu PROPERTIES
        LABELS "raw;hdr;processor;all-cfa;parity;fallback;cancellation;gpu")

    add_test(NAME StackRaw.SharedBurstGpu
        COMMAND $<TARGET_FILE:${PROJECT_NAME}> --validate-shared-burst-gpu)
    set_tests_properties(StackRaw.SharedBurstGpu PROPERTIES
        LABELS "raw;mfd;shared-burst;processor;parity;fallback;cancellation;gpu")

    add_test(NAME StackRaw.SuperResolution
        COMMAND $<TARGET_FILE:${PROJECT_NAME}> --validate-super-resolution
            "${CMAKE_CURRENT_BINARY_DIR}/validation/super-resolution")
    set_tests_properties(StackRaw.SuperResolution PROPERTIES
        LABELS "raw;bracketing;super-resolution;parity;fallback;gpu")

    add_test(NAME StackRaw.MultiFrameContracts
        COMMAND $<TARGET_FILE:${PROJECT_NAME}> --validate-multiframe-contracts)
    set_tests_properties(StackRaw.MultiFrameContracts PROPERTIES
        LABELS "raw;multiframe;contract;adapter;evidence;cfa;cpu")

    add_test(NAME StackRaw.MultiFramePhase0
        COMMAND $<TARGET_FILE:${PROJECT_NAME}> --validate-multiframe-phase0)
    set_tests_properties(StackRaw.MultiFramePhase0 PROPERTIES
        LABELS "raw;multiframe;phase0;fixture;demosaic;artifact;cpu")

    add_test(NAME StackRaw.MultiFrameEstimator
        COMMAND $<TARGET_FILE:${PROJECT_NAME}> --validate-multiframe-estimator)
    set_tests_properties(StackRaw.MultiFrameEstimator PROPERTIES
        LABELS "raw;multiframe;estimator;gaussian;accumulator;clipping;cpu")

    add_test(NAME StackRaw.MultiFramePreparedTiles
        COMMAND $<TARGET_FILE:${PROJECT_NAME}> --validate-multiframe-prepared-tiles)
    set_tests_properties(StackRaw.MultiFramePreparedTiles PROPERTIES
        LABELS "raw;multiframe;estimator;tile;mosaic;hdr;denoise;clipping;cpu")

    add_test(NAME StackRaw.SharedBurstV1
        COMMAND $<TARGET_FILE:${PROJECT_NAME}> --validate-shared-burst-v1)
    set_tests_properties(StackRaw.SharedBurstV1 PROPERTIES
        LABELS "raw;multiframe;burst;denoise;huber;uncertainty;cfa;cpu")
endif()

if (BUILD_TESTING AND TARGET StackModelServiceProtocolTests)
    add_test(
        NAME StackRestormer.ModelServiceProtocol
        COMMAND StackModelServiceProtocolTests $<TARGET_FILE:StackModelService>
    )
endif()

option(STACK_BUILD_NODE_MATH_TESTS "Build deterministic CPU/GPU node-math reference tests" ON)
if (BUILD_TESTING AND STACK_BUILD_NODE_MATH_TESTS)
    add_executable(StackNodeMathContractTests ${STACK_NODE_MATH_CONTRACT_TEST_SOURCE_FILES})
    target_include_directories(StackNodeMathContractTests PRIVATE src)

    add_executable(StackNodeMathPhase2Tests ${STACK_NODE_MATH_PHASE2_TEST_SOURCE_FILES})
    target_include_directories(StackNodeMathPhase2Tests PRIVATE src)

    add_executable(StackNodeMathPhase3Tests ${STACK_NODE_MATH_PHASE3_TEST_SOURCE_FILES})
    target_include_directories(StackNodeMathPhase3Tests PRIVATE src)

    add_executable(StackNodeMathPhase4Tests ${STACK_NODE_MATH_PHASE4_TEST_SOURCE_FILES})
    target_include_directories(StackNodeMathPhase4Tests PRIVATE src)

    add_executable(StackNodeMathPhase5Tests ${STACK_NODE_MATH_PHASE5_TEST_SOURCE_FILES})
    target_include_directories(StackNodeMathPhase5Tests PRIVATE src)

    add_executable(StackNodeMathPhase6Tests ${STACK_NODE_MATH_PHASE6_TEST_SOURCE_FILES})
    target_include_directories(StackNodeMathPhase6Tests PRIVATE src)

    add_executable(StackNodeMathChannelImageTests ${STACK_NODE_MATH_CHANNEL_IMAGE_TEST_SOURCE_FILES})
    target_include_directories(StackNodeMathChannelImageTests PRIVATE src)

    add_executable(StackNodeMathReferenceTests ${STACK_NODE_MATH_REFERENCE_TEST_SOURCE_FILES})
    target_include_directories(StackNodeMathReferenceTests PRIVATE
        src
        tools
    )
    target_link_libraries(StackNodeMathReferenceTests PRIVATE glfw)
    if (WIN32)
        target_link_libraries(StackNodeMathReferenceTests PRIVATE opengl32)
    elseif (APPLE)
        target_link_libraries(StackNodeMathReferenceTests PRIVATE "-framework OpenGL")
    elseif (UNIX)
        target_link_libraries(StackNodeMathReferenceTests PRIVATE GL)
    endif()

    add_test(NAME StackNodeMathReference.Cpu
        COMMAND StackNodeMathReferenceTests --cpu-only)
    set_tests_properties(StackNodeMathReference.Cpu PROPERTIES
        LABELS "node-math;cpu")

    add_test(NAME StackNodeMathReference.Gpu
        COMMAND StackNodeMathReferenceTests --gpu)
    set_tests_properties(StackNodeMathReference.Gpu PROPERTIES
        LABELS "node-math;gpu"
        RUN_SERIAL TRUE)

    add_test(NAME StackNodeMathContract.Phase1
        COMMAND StackNodeMathContractTests)
    set_tests_properties(StackNodeMathContract.Phase1 PROPERTIES
        LABELS "node-math;contract;cpu")

    add_test(NAME StackNodeMathSemantic.Phase2
        COMMAND StackNodeMathPhase2Tests)
    set_tests_properties(StackNodeMathSemantic.Phase2 PROPERTIES
        LABELS "node-math;semantic;color;alpha;cpu")

    add_test(NAME StackNodeMathValues.Phase3
        COMMAND StackNodeMathPhase3Tests)
    set_tests_properties(StackNodeMathValues.Phase3 PROPERTIES
        LABELS "node-math;values;types;cpu")

    add_test(NAME StackNodeMathPointwise.Phase4
        COMMAND StackNodeMathPhase4Tests)
    set_tests_properties(StackNodeMathPointwise.Phase4 PROPERTIES
        LABELS "node-math;ir;fusion;planning;cpu")

    add_test(NAME StackNodeMathCompounds.Phase5
        COMMAND StackNodeMathPhase5Tests)
    set_tests_properties(StackNodeMathCompounds.Phase5 PROPERTIES
        LABELS "node-math;compound;definition;versioning;cpu")

    add_test(NAME StackNodeMathRegions.Phase6
        COMMAND StackNodeMathPhase6Tests)
    set_tests_properties(StackNodeMathRegions.Phase6 PROPERTIES
        LABELS "node-math;regions;roi;halo;planning;cpu")

    add_test(NAME StackNodeMathReductions.Phase6B
        COMMAND StackNodeMathPhase6Tests)
    set_tests_properties(StackNodeMathReductions.Phase6B PROPERTIES
        LABELS "node-math;reduction;scalar;planning;cpu")

    add_test(NAME StackNodeMathChannels.Phase7B1
        COMMAND StackNodeMathChannelImageTests)
    set_tests_properties(StackNodeMathChannels.Phase7B1 PROPERTIES
        LABELS "node-math;channels;partial-image;semantic;cpu")

    add_test(NAME StackNodeMathPointwise.LiveGpu
        COMMAND $<TARGET_FILE:${PROJECT_NAME}> --validate-node-math-phase4)
    set_tests_properties(StackNodeMathPointwise.LiveGpu PROPERTIES
        LABELS "node-math;ir;fusion;gpu;integration"
        RUN_SERIAL TRUE)

    add_test(NAME StackNodeMathCompounds.LiveGpu
        COMMAND $<TARGET_FILE:${PROJECT_NAME}> --validate-node-math-phase5)
    set_tests_properties(StackNodeMathCompounds.LiveGpu PROPERTIES
        LABELS "node-math;compound;equivalence;gpu;integration"
        RUN_SERIAL TRUE)

    add_test(NAME StackNodeMathRegions.LiveGpu
        COMMAND $<TARGET_FILE:${PROJECT_NAME}> --validate-node-math-phase6)
    set_tests_properties(StackNodeMathRegions.LiveGpu PROPERTIES
        LABELS "node-math;regions;roi;halo;gpu;integration"
        RUN_SERIAL TRUE)

    add_test(NAME StackNodeMathReductions.LiveGpu
        COMMAND $<TARGET_FILE:${PROJECT_NAME}> --validate-node-math-phase6)
    set_tests_properties(StackNodeMathReductions.LiveGpu PROPERTIES
        LABELS "node-math;reduction;scalar;gpu;integration"
        RUN_SERIAL TRUE)
endif()

option(STACK_BUILD_GRAPH_TESTS "Build focused node graph behavior tests" ON)
if (STACK_BUILD_GRAPH_TESTS)
    add_executable(StackGraphBehaviorTests ${STACK_GRAPH_BEHAVIOR_TEST_SOURCE_FILES})
    target_include_directories(StackGraphBehaviorTests PRIVATE
        src
        src/ThirdParty
        ${imgui_SOURCE_DIR}
    )
    if (STACK_ENABLE_LIBRAW)
        target_compile_definitions(StackGraphBehaviorTests PRIVATE STACK_ENABLE_LIBRAW=1)
        target_link_libraries(StackGraphBehaviorTests PRIVATE stack_libraw)
        if (WIN32 AND MSVC)
            target_link_options(StackGraphBehaviorTests PRIVATE "/DELAYLOAD:libraw.dll")
            target_link_libraries(StackGraphBehaviorTests PRIVATE delayimp)
        endif()
    endif()
    if (WIN32)
        target_link_libraries(StackGraphBehaviorTests PRIVATE shell32 ole32)
    endif()

    add_executable(StackRawEvidenceTests ${STACK_RAW_EVIDENCE_TEST_SOURCE_FILES})
    target_include_directories(StackRawEvidenceTests PRIVATE
        src
        src/ThirdParty
    )

    add_executable(StackDngMetadataTests
        tools/tests/dng_metadata_tests.cpp
        src/Raw/DngMetadataSupplement.cpp
        src/Raw/Internal/DngTiffReader.cpp)
    target_include_directories(StackDngMetadataTests PRIVATE src)

    add_executable(StackRenderedFeatureTests ${STACK_RENDERED_FEATURE_TEST_SOURCE_FILES})
    target_include_directories(StackRenderedFeatureTests PRIVATE
        src
        src/ThirdParty
    )

    add_executable(StackPreciseCandidateTests ${STACK_PRECISE_CANDIDATE_TEST_SOURCE_FILES})
    target_include_directories(StackPreciseCandidateTests PRIVATE
        src
        src/ThirdParty
    )

    add_executable(StackOptimizerSelectionTests ${STACK_OPTIMIZER_SELECTION_TEST_SOURCE_FILES})
    target_include_directories(StackOptimizerSelectionTests PRIVATE
        src
        src/ThirdParty
    )

    add_executable(StackPreciseDryRunTests ${STACK_PRECISE_DRY_RUN_TEST_SOURCE_FILES})
    target_include_directories(StackPreciseDryRunTests PRIVATE
        src
        src/ThirdParty
    )

    add_executable(StackPreciseIntegrationTests ${STACK_PRECISE_INTEGRATION_TEST_SOURCE_FILES})
    target_include_directories(StackPreciseIntegrationTests PRIVATE
        src
        src/ThirdParty
    )

    if (BUILD_TESTING)
        add_test(NAME StackEditor.GraphBehavior
            COMMAND StackGraphBehaviorTests)
        set_tests_properties(StackEditor.GraphBehavior PROPERTIES
            LABELS "editor;node-graph;cpu")

        add_test(NAME StackRaw.Evidence
            COMMAND StackRawEvidenceTests)
        set_tests_properties(StackRaw.Evidence PROPERTIES
            LABELS "raw;analysis;cpu")

        add_test(NAME StackRaw.DngMetadata COMMAND StackDngMetadataTests)
        set_tests_properties(StackRaw.DngMetadata PROPERTIES LABELS "raw;metadata;cpu")

        add_test(NAME StackRaw.RenderedFeatures
            COMMAND StackRenderedFeatureTests)
        set_tests_properties(StackRaw.RenderedFeatures PROPERTIES
            LABELS "raw;features;cpu")

        add_test(NAME StackRaw.PreciseCandidates
            COMMAND StackPreciseCandidateTests)
        set_tests_properties(StackRaw.PreciseCandidates PROPERTIES
            LABELS "raw;optimizer;cpu")

        add_test(NAME StackRaw.OptimizerSelection
            COMMAND StackOptimizerSelectionTests)
        set_tests_properties(StackRaw.OptimizerSelection PROPERTIES
            LABELS "raw;optimizer;cpu")

        add_test(NAME StackRaw.PreciseDryRun
            COMMAND StackPreciseDryRunTests)
        set_tests_properties(StackRaw.PreciseDryRun PROPERTIES
            LABELS "raw;optimizer;cpu")

        add_test(NAME StackRaw.PreciseIntegration
            COMMAND StackPreciseIntegrationTests)
        set_tests_properties(StackRaw.PreciseIntegration PROPERTIES
            LABELS "raw;integration;cpu")
    endif()
endif()
