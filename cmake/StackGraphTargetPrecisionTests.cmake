add_executable(StackGraphTargetPrecisionTests tools/tests/graph_target_precision_tests.cpp)
target_include_directories(StackGraphTargetPrecisionTests PRIVATE src)
add_test(NAME StackRenderer.GraphTargetPrecision COMMAND StackGraphTargetPrecisionTests)
set(graph_precision_lifecycle "${CMAKE_CURRENT_BINARY_DIR}/graph_precision_lifecycle_tests.cpp")
add_custom_command(OUTPUT "${graph_precision_lifecycle}"
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tools/tests/generate_graph_precision_lifecycle_test.py"
        --output "${graph_precision_lifecycle}"
    DEPENDS tools/tests/generate_graph_precision_lifecycle_test.py
        src/Renderer/Internal/RenderPipelineResources.cpp
        src/Renderer/Internal/RenderPipelineGraphRenderTargets.cpp
        src/Renderer/Internal/RenderPipelineGraphExecution.cpp
        src/Renderer/Internal/RenderPipelineGraphExecutionHelpers.cpp
        src/Renderer/Internal/RenderPipelineGraphTextureCache.cpp
        src/Renderer/Internal/RenderPipelineRawDevelopStageCache.cpp
        src/Renderer/Internal/RenderPipelineCalibration.cpp
    VERBATIM)
add_executable(StackGraphPrecisionLifecycleTests "${graph_precision_lifecycle}")
target_include_directories(StackGraphPrecisionLifecycleTests PRIVATE src)
add_test(NAME StackRenderer.GraphPrecisionLifecycle COMMAND StackGraphPrecisionLifecycleTests)
set_tests_properties(StackRenderer.GraphTargetPrecision StackRenderer.GraphPrecisionLifecycle
    PROPERTIES LABELS "renderer;precision;cpu" TIMEOUT 30)
