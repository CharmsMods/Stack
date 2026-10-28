set(graph_gateway_drag_test "${CMAKE_CURRENT_BINARY_DIR}/graph_gateway_drag_tests.cpp")
add_custom_command(OUTPUT "${graph_gateway_drag_test}"
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tools/tests/graph_gateway_drag_tests.py"
        --output "${graph_gateway_drag_test}"
    DEPENDS tools/tests/graph_gateway_drag_tests.py
        src/Editor/NodeGraph/UI/EditorNodeGraphUILinksAndGroups.cpp
    VERBATIM)
add_executable(StackGraphGatewayDragTests "${graph_gateway_drag_test}")
add_test(NAME StackGraph.GatewayDrag COMMAND StackGraphGatewayDragTests)
set_tests_properties(StackGraph.GatewayDrag PROPERTIES LABELS "graph;interaction;ui;cpu" TIMEOUT 30)
