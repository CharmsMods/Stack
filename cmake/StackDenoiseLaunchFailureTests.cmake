set(denoise_launch_failure_test "${CMAKE_CURRENT_BINARY_DIR}/denoise_launch_failure_tests.cpp")
add_custom_command(OUTPUT "${denoise_launch_failure_test}"
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tools/tests/denoise_launch_failure_tests.py"
        --output "${denoise_launch_failure_test}"
    DEPENDS tools/tests/denoise_launch_failure_tests.py
        src/Renderer/Internal/RenderPipelineRawRgbDenoise.cpp
        src/Renderer/RawRgbDenoiseState.h
        src/Editor/Internal/EditorRenderWorkerSessions.cpp
    VERBATIM)
add_executable(StackDenoiseLaunchFailureTests "${denoise_launch_failure_test}"
    src/Renderer/RawRgbDenoiseState.cpp)
target_include_directories(StackDenoiseLaunchFailureTests PRIVATE src)
target_link_libraries(StackDenoiseLaunchFailureTests PRIVATE Threads::Threads)
add_test(NAME StackRenderer.DenoiseLaunchFailure COMMAND StackDenoiseLaunchFailureTests)
set_tests_properties(StackRenderer.DenoiseLaunchFailure PROPERTIES LABELS "render;denoise;async;cpu" TIMEOUT 30)
