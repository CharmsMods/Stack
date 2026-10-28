set(graph_socket_occlusion_test "${CMAKE_CURRENT_BINARY_DIR}/graph_socket_occlusion_tests.cpp")
add_custom_command(OUTPUT "${graph_socket_occlusion_test}"
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tools/tests/graph_socket_occlusion_tests.py"
        --output "${graph_socket_occlusion_test}"
    DEPENDS tools/tests/graph_socket_occlusion_tests.py
        src/Editor/NodeGraph/UI/EditorNodeGraphUIHitTesting.cpp
    VERBATIM)
add_executable(StackGraphSocketOcclusionTests "${graph_socket_occlusion_test}")
add_test(NAME StackGraph.SocketOcclusion COMMAND StackGraphSocketOcclusionTests)
set_tests_properties(StackGraph.SocketOcclusion PROPERTIES LABELS "graph;interaction;ui;cpu" TIMEOUT 30)
