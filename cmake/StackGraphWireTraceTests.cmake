set(graph_wire_trace_test "${CMAKE_CURRENT_BINARY_DIR}/generated/graph_wire_trace_tests.cpp")
add_custom_command(OUTPUT "${graph_wire_trace_test}"
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tools/tests/graph_wire_trace_allocation_tests.py"
        --output "${graph_wire_trace_test}"
    DEPENDS tools/tests/graph_wire_trace_allocation_tests.py
        src/Editor/NodeGraph/UI/EditorNodeGraphUILinksAndGroups.cpp
        src/Editor/NodeGraph/UI/ContinuousLinkStroke.cpp
        src/Editor/NodeGraph/UI/ContinuousLinkStroke.h
    VERBATIM)
add_executable(StackGraphWireTraceTests "${graph_wire_trace_test}")
target_include_directories(StackGraphWireTraceTests PRIVATE src)
target_link_libraries(StackGraphWireTraceTests PRIVATE imgui_core)
add_test(NAME StackGraph.WireTraceAllocations COMMAND StackGraphWireTraceTests)
set_tests_properties(StackGraph.WireTraceAllocations PROPERTIES LABELS "graph;ui;cpu" TIMEOUT 30)
