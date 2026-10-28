set(graph_drop_test "${CMAKE_CURRENT_BINARY_DIR}/generated/graph_drop_geometry_tests.cpp")
add_custom_command(OUTPUT "${graph_drop_test}"
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tools/tests/graph_drop_geometry_tests.py"
        --output "${graph_drop_test}"
    DEPENDS tools/tests/graph_drop_geometry_tests.py
        src/Editor/NodeGraph/UI/EditorNodeGraphUICanvas.cpp
        src/Editor/NodeGraph/UI/EditorNodeGraphUICamera.cpp
    VERBATIM)
add_executable(StackGraphDropGeometryTests "${graph_drop_test}")
add_test(NAME StackEditor.GraphDropGeometry COMMAND StackGraphDropGeometryTests)
set_tests_properties(StackEditor.GraphDropGeometry PROPERTIES
    LABELS "editor;graph;input;cpu" TIMEOUT 30)
