set(viewport_cursor_overlay_test "${CMAKE_CURRENT_BINARY_DIR}/generated/viewport_cursor_overlay_tests.cpp")
add_custom_command(OUTPUT "${viewport_cursor_overlay_test}"
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tools/tests/viewport_cursor_overlay_tests.py"
        --output "${viewport_cursor_overlay_test}"
    DEPENDS tools/tests/viewport_cursor_overlay_tests.py tools/tests/viewport_cursor_overlay_tests.cpp
        src/Editor/UI/EditorViewport.cpp
    VERBATIM)
add_executable(StackViewportCursorOverlayTests "${viewport_cursor_overlay_test}")
target_link_libraries(StackViewportCursorOverlayTests PRIVATE imgui_core)
add_test(NAME StackEditor.ViewportCursorOverlay COMMAND StackViewportCursorOverlayTests)
set_tests_properties(StackEditor.ViewportCursorOverlay PROPERTIES LABELS "viewport;ui;cpu" TIMEOUT 30)
