set(raw_node_lifetime_test "${CMAKE_CURRENT_BINARY_DIR}/generated/raw_node_resource_lifetime_tests.cpp")
add_custom_command(OUTPUT "${raw_node_lifetime_test}"
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tools/tests/raw_node_resource_lifetime_tests.py"
        --output "${raw_node_lifetime_test}"
    DEPENDS tools/tests/raw_node_resource_lifetime_tests.py
        src/Renderer/Internal/RenderPipelineResources.cpp
        src/Renderer/Internal/RenderPipelineGraphExecution.cpp
        src/Renderer/Internal/RenderPipelineGraphSchedule.h
        src/Renderer/MaskRenderTypes.h
        src/Project/GraphSnapshotBuilder.cpp
        src/Raw/RawGpuPipeline.cpp
    VERBATIM)
add_executable(StackRawNodeResourceLifetimeTests "${raw_node_lifetime_test}")
target_include_directories(StackRawNodeResourceLifetimeTests PRIVATE src)
add_test(NAME StackRenderer.RawNodeResourceLifetime COMMAND StackRawNodeResourceLifetimeTests)
set_tests_properties(StackRenderer.RawNodeResourceLifetime PROPERTIES
    LABELS "renderer;raw;resources;cpu" TIMEOUT 30)
