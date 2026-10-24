set(denoise_output_provenance_test "${CMAKE_CURRENT_BINARY_DIR}/denoise_output_provenance_tests.cpp")
add_custom_command(OUTPUT "${denoise_output_provenance_test}"
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tools/tests/denoise_output_provenance_tests.py"
        --output "${denoise_output_provenance_test}"
    DEPENDS tools/tests/denoise_output_provenance_tests.py
        src/Renderer/RenderPipeline.h
        src/Renderer/Internal/RenderPipelineGraphExecutionRuntime.h
        src/Renderer/Internal/RenderPipelineGraphExecutionRuntime.cpp
        src/Renderer/Internal/RenderPipelineGraphExecutionEvaluateImage.cpp
        src/Renderer/Internal/RenderPipelineGraphExecutionEvaluateMask.cpp
        src/Renderer/Internal/RenderPipelineGraphExecutionEvaluateScalar.cpp
        src/Renderer/Internal/RenderPipelineGraphTextureCache.cpp
        src/Renderer/Internal/RenderPipelineRawDevelopStageCache.cpp
        src/Renderer/Internal/RenderPipelineRawRgbDenoise.cpp
        src/Renderer/Internal/RenderPipelineGraphRawDevelopmentNode.cpp
        src/Renderer/Internal/RenderPipelineGraphRawDevelopmentOutputStages.cpp
        src/Renderer/Internal/RenderPipelineViewportRegion.cpp
        src/Renderer/Internal/RenderPipelineViewportDependency.cpp
    VERBATIM)
add_executable(StackDenoiseOutputProvenanceTests "${denoise_output_provenance_test}")
target_compile_features(StackDenoiseOutputProvenanceTests PRIVATE cxx_std_17)
target_include_directories(StackDenoiseOutputProvenanceTests PRIVATE src)
add_test(NAME StackRenderer.DenoiseOutputProvenance COMMAND StackDenoiseOutputProvenanceTests)
set_tests_properties(StackRenderer.DenoiseOutputProvenance PROPERTIES LABELS "render;denoise;cache;cpu" TIMEOUT 30)
