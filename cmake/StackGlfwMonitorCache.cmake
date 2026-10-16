# Reproducible adaptation of the pinned backend, leaving FetchContent untouched.
option(STACK_CACHE_GLFW_MONITORS "Cache Windows ImGui GLFW monitor information" ON)
set(STACK_GLFW_BACKEND_SOURCE "${CMAKE_BINARY_DIR}/generated/imgui_impl_glfw_stack.cpp")
file(READ "${imgui_SOURCE_DIR}/backends/imgui_impl_glfw.cpp" STACK_GLFW_BACKEND_TEXT)

function(stack_glfw_replace_once marker replacement)
    string(FIND "${STACK_GLFW_BACKEND_TEXT}" "${marker}" marker_at)
    if(marker_at LESS 0)
        message(FATAL_ERROR "Pinned ImGui GLFW adaptation anchor missing: ${marker}")
    endif()
    string(LENGTH "${marker}" marker_length)
    math(EXPR tail_at "${marker_at} + ${marker_length}")
    string(SUBSTRING "${STACK_GLFW_BACKEND_TEXT}" ${tail_at} -1 tail)
    string(FIND "${tail}" "${marker}" duplicate_at)
    if(NOT duplicate_at LESS 0)
        message(FATAL_ERROR "Pinned ImGui GLFW adaptation anchor is ambiguous: ${marker}")
    endif()
    string(REPLACE "${marker}" "${replacement}" result "${STACK_GLFW_BACKEND_TEXT}")
    set(STACK_GLFW_BACKEND_TEXT "${result}" PARENT_SCOPE)
endfunction()

stack_glfw_replace_once("    bool                    IsWayland;"
    "    bool                    IsWayland;\n    Stack::Platform::GlfwMonitorRefreshState MonitorRefresh;")
stack_glfw_replace_once("    // This function is technically part of the API even if we stopped using the callback, so leaving it around."
    "    Stack::Platform::InvalidateGlfwMonitors();")
stack_glfw_replace_once("    WNDPROC prev_wndproc = bd->PrevWndProc;"
    "    if (msg == WM_DISPLAYCHANGE || msg == WM_SETTINGCHANGE || msg == WM_DPICHANGED)\n        Stack::Platform::InvalidateGlfwMonitors();\n\n    WNDPROC prev_wndproc = bd->PrevWndProc;")
stack_glfw_replace_once("        glfwSetWindowAttrib(window, GLFW_MOUSE_PASSTHROUGH, window_no_input);"
    "        if (Stack::Platform::NeedsGlfwMousePassthroughUpdate(glfwGetWindowAttrib(window, GLFW_MOUSE_PASSTHROUGH), window_no_input))\n            glfwSetWindowAttrib(window, GLFW_MOUSE_PASSTHROUGH, window_no_input);")

set(update_start "static void ImGui_ImplGlfw_UpdateMonitors()\n{")
set(update_end "\n// For GFLW 3.2 + Windows:")
string(FIND "${STACK_GLFW_BACKEND_TEXT}" "${update_start}" update_at)
string(FIND "${STACK_GLFW_BACKEND_TEXT}" "${update_end}" update_end_at)
if(update_at LESS 0 OR update_end_at LESS update_at)
    message(FATAL_ERROR "Pinned ImGui GLFW monitor-function boundaries changed")
endif()
math(EXPR update_length "${update_end_at} - ${update_at}")
string(SUBSTRING "${STACK_GLFW_BACKEND_TEXT}" ${update_at} ${update_length} upstream_update)
file(READ "${CMAKE_CURRENT_SOURCE_DIR}/src/Platform/ImGuiGlfwMonitorRefresh.inc" adapted_update)
stack_glfw_replace_once("${upstream_update}" "${adapted_update}\n")
set(STACK_GLFW_BACKEND_TEXT "#include \"Platform/GlfwMonitorRefresh.h\"\n${STACK_GLFW_BACKEND_TEXT}")
file(CONFIGURE OUTPUT "${STACK_GLFW_BACKEND_SOURCE}" CONTENT "${STACK_GLFW_BACKEND_TEXT}" @ONLY)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
    "${imgui_SOURCE_DIR}/backends/imgui_impl_glfw.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/Platform/ImGuiGlfwMonitorRefresh.inc")
