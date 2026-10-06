add_executable(StackWorkspaceSwitcherTests tools/tests/workspace_switcher_tests.cpp)
target_include_directories(StackWorkspaceSwitcherTests PRIVATE src)
add_test(NAME StackApp.WorkspaceSwitcher COMMAND StackWorkspaceSwitcherTests)
set_tests_properties(StackApp.WorkspaceSwitcher PROPERTIES LABELS "app;workspace;cpu")
add_executable(StackWorkspaceCompositorTests
    tools/tests/workspace_compositor_tests.cpp
    src/App/WorkspaceSwitcherDrawing.cpp
    src/Renderer/WorkspaceCompositor.cpp
    src/Renderer/WorkspacePreviewCache.cpp
    src/Renderer/RawImageBackdrop.cpp
    src/Renderer/RawImageBackdropBlur.cpp
    src/Renderer/RawImageBackdropNoise.cpp
    src/Renderer/GLLoader.cpp
    src/Renderer/GLHelpers.cpp)
target_include_directories(StackWorkspaceCompositorTests PRIVATE src)
target_link_libraries(StackWorkspaceCompositorTests PRIVATE imgui_core)
if(WIN32)
    target_link_libraries(StackWorkspaceCompositorTests PRIVATE opengl32)
elseif(APPLE)
    target_link_libraries(StackWorkspaceCompositorTests PRIVATE "-framework OpenGL")
else()
    target_link_libraries(StackWorkspaceCompositorTests PRIVATE GL)
endif()
add_test(NAME StackApp.WorkspaceCompositor COMMAND StackWorkspaceCompositorTests)
set_tests_properties(StackApp.WorkspaceCompositor PROPERTIES LABELS "app;workspace;gpu")
