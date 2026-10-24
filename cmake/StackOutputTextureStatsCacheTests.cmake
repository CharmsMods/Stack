find_package(Python3 REQUIRED COMPONENTS Interpreter)
set(STACK_OUTPUT_STATS_TEST_SOURCE "${CMAKE_CURRENT_BINARY_DIR}/generated/output_texture_stats_cache_tests.cpp")
add_custom_command(OUTPUT "${STACK_OUTPUT_STATS_TEST_SOURCE}"
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tools/tests/output_texture_stats_cache_tests.py"
        --output "${STACK_OUTPUT_STATS_TEST_SOURCE}"
    DEPENDS
        tools/tests/output_texture_stats_cache_tests.py
        src/Renderer/RenderPipeline.h
        src/Renderer/RenderPipeline.cpp
        src/Renderer/OutputTextureStatsCache.h
        src/Renderer/Internal/RenderPipelineResources.cpp
        src/Renderer/Internal/RenderPipelineReadback.cpp
    VERBATIM)
add_executable(StackOutputTextureStatsCacheTests "${STACK_OUTPUT_STATS_TEST_SOURCE}")
target_include_directories(StackOutputTextureStatsCacheTests PRIVATE src)
add_test(NAME StackRenderer.OutputTextureStatsCache COMMAND StackOutputTextureStatsCacheTests)
set_tests_properties(StackRenderer.OutputTextureStatsCache PROPERTIES LABELS "renderer;diagnostics;cache;cpu" TIMEOUT 30)
