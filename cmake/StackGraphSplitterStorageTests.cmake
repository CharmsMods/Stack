set(graph_splitter_storage_test "${CMAKE_CURRENT_BINARY_DIR}/generated/graph_splitter_storage_tests.cpp")
add_custom_command(OUTPUT "${graph_splitter_storage_test}"
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tools/tests/graph_splitter_storage_tests.py"
        --output "${graph_splitter_storage_test}"
    DEPENDS tools/tests/graph_splitter_storage_tests.py tools/tests/graph_splitter_storage_tests.cpp
        src/Editor/NodeGraph/UI/EditorNodeGraphUICanvas.cpp src/Editor/NodeGraph/EditorNodeGraphUI.h
    VERBATIM)
add_executable(StackGraphSplitterStorageTests "${graph_splitter_storage_test}")
target_link_libraries(StackGraphSplitterStorageTests PRIVATE imgui_core)
add_test(NAME StackGraph.SplitterStorage COMMAND StackGraphSplitterStorageTests)
set_tests_properties(StackGraph.SplitterStorage PROPERTIES LABELS "graph;ui;cpu" TIMEOUT 30)
