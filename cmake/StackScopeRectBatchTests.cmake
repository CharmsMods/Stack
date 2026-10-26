set(scope_rect_batch_test "${CMAKE_CURRENT_BINARY_DIR}/generated/scope_rect_batch_tests.cpp")
add_custom_command(OUTPUT "${scope_rect_batch_test}"
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tools/tests/scope_rect_batch_tests.py"
        --output "${scope_rect_batch_test}"
    DEPENDS tools/tests/scope_rect_batch_tests.py
        src/Editor/UI/ScopeRectBatch.h src/Editor/UI/EditorScopes.cpp
    VERBATIM)
add_executable(StackScopeRectBatchTests "${scope_rect_batch_test}" src/Editor/GraphScopeData.cpp)
target_include_directories(StackScopeRectBatchTests PRIVATE src)
target_link_libraries(StackScopeRectBatchTests PRIVATE imgui_core)
add_test(NAME StackEditor.ScopeRectBatch COMMAND StackScopeRectBatchTests)
set_tests_properties(StackEditor.ScopeRectBatch PROPERTIES LABELS "editor;scopes;ui;cpu" TIMEOUT 30)
