# Explicit diagnostic source overlay. It replaces only the saved UI hot paths;
# shared tracing, input, rendering services and unrelated working changes stay
# identical between comparison executables. The default app uses live sources.
set(STACK_UI_PERF_BASELINE_DIR "" CACHE PATH "Instrumented pre-optimization UI sources for a bounded comparison")
option(STACK_UI_PERF_LINK_MAP "Write a linker map for bounded UI invalidation profiling" OFF)
if(STACK_UI_PERF_BASELINE_DIR)
    foreach(relative
        Editor/NodeGraph/EditorNodeGraphUI.cpp
        Editor/NodeGraph/UI/EditorNodeGraphUICamera.cpp
        Editor/NodeGraph/UI/EditorNodeGraphUICanvas.cpp
        Editor/NodeGraph/UI/EditorNodeGraphUIHitTesting.cpp
        Editor/NodeGraph/UI/EditorNodeGraphUILinksAndGroups.cpp
        Editor/NodeGraph/UI/EditorNodeGraphUINodeBrowser.cpp
        Editor/NodeGraph/UI/EditorNodeGraphUIPreviews.cpp
        Editor/Internal/EditorModuleRenderRequests.cpp
        Editor/Internal/EditorModuleViewportPreview.cpp
        Editor/Internal/EditorModuleRenderAdoption.cpp
        App/AppRawMaskToolbar.cpp
        App/AppWindowTitleBarBridge.cpp)
        get_filename_component(filename "${relative}" NAME)
        set(replacement "${STACK_UI_PERF_BASELINE_DIR}/${filename}")
        if(NOT EXISTS "${replacement}")
            message(FATAL_ERROR "UI performance baseline source missing: ${replacement}")
        endif()
        list(REMOVE_ITEM STACK_APP_SOURCE_FILES "${CMAKE_CURRENT_SOURCE_DIR}/src/${relative}")
        list(APPEND STACK_APP_SOURCE_FILES "${replacement}")
    endforeach()
endif()
