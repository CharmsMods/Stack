add_executable(StackNodeOpenTextureLifetimeTests
    tools/tests/node_open_texture_lifetime_tests.cpp
    src/Editor/UI/NodeOpenAppearance.cpp
    src/Renderer/ViewportTextureCopy.cpp
    src/Renderer/GLHelpers.cpp
    src/Renderer/GLLoader.cpp)
target_include_directories(StackNodeOpenTextureLifetimeTests PRIVATE src)
target_link_libraries(StackNodeOpenTextureLifetimeTests PRIVATE imgui_core)
if(WIN32)
    target_link_libraries(StackNodeOpenTextureLifetimeTests PRIVATE opengl32)
elseif(APPLE)
    target_link_libraries(StackNodeOpenTextureLifetimeTests PRIVATE "-framework OpenGL")
elseif(UNIX)
    target_link_libraries(StackNodeOpenTextureLifetimeTests PRIVATE GL)
endif()
add_test(NAME StackEditor.NodeOpenTextureLifetime COMMAND StackNodeOpenTextureLifetimeTests)
set_tests_properties(StackEditor.NodeOpenTextureLifetime PROPERTIES LABELS "editor;transition;gpu" TIMEOUT 30)
add_test(NAME StackEditor.NodeOpenTextureLifetimeWiring
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tools/tests/node_open_texture_lifetime_wiring_tests.py")
set_tests_properties(StackEditor.NodeOpenTextureLifetimeWiring PROPERTIES LABELS "editor;transition;cpu" TIMEOUT 30)
