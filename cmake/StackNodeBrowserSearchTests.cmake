set(node_browser_search_test "${CMAKE_CURRENT_BINARY_DIR}/generated/node_browser_search_tests.cpp")
add_custom_command(OUTPUT "${node_browser_search_test}"
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tools/tests/node_browser_search_tests.py"
        --output "${node_browser_search_test}"
    DEPENDS tools/tests/node_browser_search_tests.py tools/tests/node_browser_search_tests.cpp
        src/Editor/NodeGraph/UI/EditorNodeGraphUINodeBrowser.cpp
    VERBATIM)
add_executable(StackNodeBrowserSearchTests "${node_browser_search_test}")
target_link_libraries(StackNodeBrowserSearchTests PRIVATE imgui_core)
add_test(NAME StackEditor.NodeBrowserSearch COMMAND StackNodeBrowserSearchTests)
set_tests_properties(StackEditor.NodeBrowserSearch PROPERTIES LABELS "graph;ui;cpu" TIMEOUT 30)
