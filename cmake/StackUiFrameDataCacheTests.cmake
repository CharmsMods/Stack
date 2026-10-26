set(ui_frame_data_cache_test "${CMAKE_CURRENT_BINARY_DIR}/generated/ui_frame_data_cache_tests.cpp")
add_custom_command(OUTPUT "${ui_frame_data_cache_test}"
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tools/tests/ui_frame_data_cache_tests.py"
        --output "${ui_frame_data_cache_test}"
    DEPENDS tools/tests/ui_frame_data_cache_tests.py
        src/Editor/Internal/EditorModuleActivity.cpp
        src/Editor/NodeGraph/EditorNodeGraphUI.cpp
        src/Editor/NodeGraph/EditorNodeGraphUI.h
        src/Editor/NodeGraph/Model/EditorNodeGraphMutation.cpp
    VERBATIM)
add_executable(StackUiFrameDataCacheTests "${ui_frame_data_cache_test}")
add_test(NAME StackEditor.UiFrameDataCache COMMAND StackUiFrameDataCacheTests)
set_tests_properties(StackEditor.UiFrameDataCache PROPERTIES
    LABELS "editor;ui;cache;cpu" TIMEOUT 30)
