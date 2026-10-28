add_executable(StackBackdropBlurUniformTests
    tools/tests/backdrop_blur_uniform_tests.cpp
    src/Renderer/RawImageBackdropBlur.cpp
    src/Renderer/GLLoader.cpp
    src/Renderer/GLHelpers.cpp)
target_include_directories(StackBackdropBlurUniformTests PRIVATE src)
target_link_libraries(StackBackdropBlurUniformTests PRIVATE glfw imgui_core)
if(WIN32)
    target_link_libraries(StackBackdropBlurUniformTests PRIVATE opengl32)
elseif(APPLE)
    target_link_libraries(StackBackdropBlurUniformTests PRIVATE "-framework OpenGL")
else()
    target_link_libraries(StackBackdropBlurUniformTests PRIVATE GL)
endif()
add_test(NAME StackRenderer.BackdropBlurUniforms COMMAND StackBackdropBlurUniformTests)
set_tests_properties(StackRenderer.BackdropBlurUniforms PROPERTIES LABELS "renderer;ui;gpu" TIMEOUT 30)
