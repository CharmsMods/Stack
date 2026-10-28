set(raw_clipboard_refresh_test "${CMAKE_CURRENT_BINARY_DIR}/raw_clipboard_refresh_tests.cpp")
add_custom_command(OUTPUT "${raw_clipboard_refresh_test}"
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tools/tests/raw_clipboard_refresh_tests.py"
        --output "${raw_clipboard_refresh_test}"
    DEPENDS tools/tests/raw_clipboard_refresh_tests.py
        src/Editor/EditorModule.h
        src/Editor/Internal/EditorModuleRawAttributeTransfer.cpp
        src/Editor/Internal/RawWorkspace/EditorModuleRawGalleryActions.cpp
    VERBATIM)
add_executable(StackRawClipboardRefreshTests "${raw_clipboard_refresh_test}")
add_test(NAME StackEditor.RawClipboardRefresh COMMAND StackRawClipboardRefreshTests)
set_tests_properties(StackEditor.RawClipboardRefresh PROPERTIES LABELS "editor;gallery;clipboard;cpu" TIMEOUT 30)
