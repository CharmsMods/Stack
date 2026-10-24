set(color_warp_diagnostic_test "${CMAKE_CURRENT_BINARY_DIR}/color_warp_diagnostic_callback_tests.cpp")
add_custom_command(OUTPUT "${color_warp_diagnostic_test}"
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tools/tests/color_warp_diagnostic_callback_tests.py"
        --output "${color_warp_diagnostic_test}"
    DEPENDS tools/tests/color_warp_diagnostic_callback_tests.py
        src/Editor/EditorModuleTypes.h
        src/Editor/Internal/RawLab/EditorModuleRawLabPreview.cpp
        src/Editor/Internal/RawLab/RawLabColorSurface.cpp
        src/Editor/Internal/RawWorkspace/EditorModuleRawWorkspacePreview.cpp
    VERBATIM)
add_executable(StackColorWarpDiagnosticCallbackTests "${color_warp_diagnostic_test}" src/Async/TaskSystem.cpp)
target_include_directories(StackColorWarpDiagnosticCallbackTests PRIVATE src)
target_link_libraries(StackColorWarpDiagnosticCallbackTests PRIVATE Threads::Threads)
add_test(NAME StackEditor.ColorWarpDiagnosticCallback COMMAND StackColorWarpDiagnosticCallbackTests)
set_tests_properties(StackEditor.ColorWarpDiagnosticCallback PROPERTIES LABELS "editor;color;async;cpu" TIMEOUT 30)
