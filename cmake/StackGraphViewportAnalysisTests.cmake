add_executable(StackGraphViewportAnalysisTests tools/tests/graph_viewport_analysis_policy_tests.cpp)
target_include_directories(StackGraphViewportAnalysisTests PRIVATE src)
add_test(NAME StackEditor.GraphViewportAnalysis COMMAND StackGraphViewportAnalysisTests)
set_tests_properties(StackEditor.GraphViewportAnalysis PROPERTIES
    LABELS "editor;viewport;analysis;cpu" TIMEOUT 30)
add_test(NAME StackEditor.GraphViewportAnalysisWiring
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tools/tests/graph_viewport_analysis_wiring_tests.py")
set_tests_properties(StackEditor.GraphViewportAnalysisWiring PROPERTIES
    LABELS "editor;viewport;analysis;cpu" TIMEOUT 30)
