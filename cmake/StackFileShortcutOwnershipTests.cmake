set(file_shortcut_ownership_test "${CMAKE_CURRENT_BINARY_DIR}/file_shortcut_ownership_tests.cpp")
add_custom_command(OUTPUT "${file_shortcut_ownership_test}"
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tools/tests/file_shortcut_ownership_tests.py"
        --output "${file_shortcut_ownership_test}"
    DEPENDS tools/tests/file_shortcut_ownership_tests.py
        src/App/AppShell.cpp
        src/App/WorkspaceInputScope.h
        src/App/WorkspacePresentation.h
        src/Editor/EditorModule.cpp
        src/Editor/Internal/EditorModuleGraphCatalog.cpp
    VERBATIM)
add_executable(StackFileShortcutOwnershipTests "${file_shortcut_ownership_test}")
target_include_directories(StackFileShortcutOwnershipTests PRIVATE src)
target_link_libraries(StackFileShortcutOwnershipTests PRIVATE imgui_core)
add_test(NAME StackApp.FileShortcutOwnership COMMAND StackFileShortcutOwnershipTests)
set_tests_properties(StackApp.FileShortcutOwnership PROPERTIES LABELS "app;input;ui;cpu" TIMEOUT 30)
