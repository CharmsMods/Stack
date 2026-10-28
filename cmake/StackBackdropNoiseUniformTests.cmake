add_executable(StackBackdropNoiseUniformTests
    tools/tests/backdrop_noise_uniform_tests.cpp
    src/Renderer/RawImageBackdropNoise.cpp
    src/Renderer/GLLoader.cpp
    src/Renderer/GLHelpers.cpp)
target_include_directories(StackBackdropNoiseUniformTests PRIVATE src)
target_link_libraries(StackBackdropNoiseUniformTests PRIVATE glfw imgui_core)
if(WIN32)
    target_link_libraries(StackBackdropNoiseUniformTests PRIVATE opengl32)
elseif(APPLE)
    target_link_libraries(StackBackdropNoiseUniformTests PRIVATE "-framework OpenGL")
else()
    target_link_libraries(StackBackdropNoiseUniformTests PRIVATE GL)
endif()
add_test(NAME StackRenderer.BackdropNoiseUniforms COMMAND StackBackdropNoiseUniformTests)
set_tests_properties(StackRenderer.BackdropNoiseUniforms PROPERTIES LABELS "renderer;ui;gpu" TIMEOUT 90)
