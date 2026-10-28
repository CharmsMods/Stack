add_executable(StackGraphFrameTextureLifetimeTests
    tools/tests/graph_frame_texture_lifetime_tests.cpp
    src/Editor/UI/GraphFrameTransition.cpp
    src/Editor/Internal/RawWorkspace/RawViewportFadeRenderer.cpp
    src/Renderer/GpuMemoryBudget.cpp
    src/Raw/RawGpuMemoryBudget.cpp
    src/Renderer/GLHelpers.cpp
    src/Renderer/GLLoader.cpp)
target_include_directories(StackGraphFrameTextureLifetimeTests PRIVATE src)
target_link_libraries(StackGraphFrameTextureLifetimeTests PRIVATE imgui_core)
if(WIN32)
    target_link_libraries(StackGraphFrameTextureLifetimeTests PRIVATE opengl32 dxgi)
elseif(APPLE)
    target_link_libraries(StackGraphFrameTextureLifetimeTests PRIVATE "-framework OpenGL")
elseif(UNIX)
    target_link_libraries(StackGraphFrameTextureLifetimeTests PRIVATE GL)
endif()
add_test(NAME StackEditor.GraphFrameTextureLifetime COMMAND StackGraphFrameTextureLifetimeTests)
set_tests_properties(StackEditor.GraphFrameTextureLifetime PROPERTIES LABELS "editor;transition;gpu" TIMEOUT 30)
