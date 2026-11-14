add_executable(StackStartupSceneTests
    tools/tests/startup_scene_tests.cpp
    src/App/Startup/StartupScene.cpp
    src/App/Startup/StartupPlatform.cpp
    src/Renderer/GLHelpers.cpp src/Renderer/GLLoader.cpp
    src/ThirdParty/stb_image_impl.cpp)
target_include_directories(StackStartupSceneTests PRIVATE src src/ThirdParty)
target_link_libraries(StackStartupSceneTests PRIVATE glfw)
if(WIN32)
    target_link_libraries(StackStartupSceneTests PRIVATE opengl32 gdi32 user32)
endif()
