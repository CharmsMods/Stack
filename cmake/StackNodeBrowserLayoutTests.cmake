set(node_browser_layout_test "${CMAKE_CURRENT_BINARY_DIR}/generated/node_browser_layout_tests.cpp")
add_custom_command(OUTPUT "${node_browser_layout_test}"
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tools/tests/node_browser_layout_tests.py"
        --output "${node_browser_layout_test}"
    DEPENDS tools/tests/node_browser_layout_tests.py tools/tests/node_browser_layout_tests.cpp
        src/Editor/NodeGraph/UI/EditorNodeGraphUINodeBrowser.cpp
    VERBATIM)
add_executable(StackNodeBrowserLayoutTests "${node_browser_layout_test}")
target_link_libraries(StackNodeBrowserLayoutTests PRIVATE imgui_core)
add_test(NAME StackEditor.NodeBrowserLayout COMMAND StackNodeBrowserLayoutTests)
set_tests_properties(StackEditor.NodeBrowserLayout PROPERTIES LABELS "graph;ui;cpu" TIMEOUT 30)
