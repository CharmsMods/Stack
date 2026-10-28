set(graph_split_confirm_test "${CMAKE_CURRENT_BINARY_DIR}/graph_split_confirm_tests.cpp")
add_custom_command(OUTPUT "${graph_split_confirm_test}"
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tools/tests/graph_split_confirm_tests.py"
        --output "${graph_split_confirm_test}"
    DEPENDS tools/tests/graph_split_confirm_tests.py
        src/Editor/NodeGraph/UI/EditorNodeGraphUIInteraction.cpp
        src/Editor/EditorModule.cpp
    VERBATIM)
add_executable(StackGraphSplitConfirmTests "${graph_split_confirm_test}")
target_link_libraries(StackGraphSplitConfirmTests PRIVATE imgui_core)
add_test(NAME StackGraph.SplitConfirm COMMAND StackGraphSplitConfirmTests)
set_tests_properties(StackGraph.SplitConfirm PROPERTIES LABELS "graph;interaction;ui;cpu" TIMEOUT 30)
