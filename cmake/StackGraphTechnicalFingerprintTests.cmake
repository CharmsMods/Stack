set(graph_technical_fingerprint "${CMAKE_CURRENT_BINARY_DIR}/generated/graph_technical_fingerprint_tests.cpp")
add_custom_command(OUTPUT "${graph_technical_fingerprint}"
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tools/tests/graph_technical_fingerprint_tests.py"
        --output "${graph_technical_fingerprint}"
    DEPENDS tools/tests/graph_technical_fingerprint_tests.py
        src/Renderer/Internal/RenderPipelineGraphExecutionFingerprintImage.cpp
        src/Renderer/Internal/RenderPipelineGraphExecutionHelpers.h
        src/Renderer/Internal/RenderPipelineGraphViewport.cpp
    VERBATIM)
add_executable(StackGraphTechnicalFingerprintTests "${graph_technical_fingerprint}"
    src/Renderer/Internal/RenderPipelineGraphSchedule.cpp)
target_include_directories(StackGraphTechnicalFingerprintTests PRIVATE src)
add_test(NAME StackRenderer.GraphTechnicalFingerprint COMMAND StackGraphTechnicalFingerprintTests)
set_tests_properties(StackRenderer.GraphTechnicalFingerprint PROPERTIES LABELS "renderer;graph;cache;cpu" TIMEOUT 30)
