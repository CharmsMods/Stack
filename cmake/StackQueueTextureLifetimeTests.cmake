set(queue_texture_lifetime_test "${CMAKE_CURRENT_BINARY_DIR}/generated/queue_texture_lifetime_tests.cpp")
add_custom_command(OUTPUT "${queue_texture_lifetime_test}"
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tools/tests/queue_texture_lifetime_tests.py"
        --output "${queue_texture_lifetime_test}"
    DEPENDS tools/tests/queue_texture_lifetime_tests.py tools/tests/queue_texture_lifetime_tests.cpp
        src/Queue/QueueModule.cpp src/Queue/QueueView.cpp src/App/AppShell.cpp
    VERBATIM)
add_executable(StackQueueTextureLifetimeTests "${queue_texture_lifetime_test}" src/Renderer/GLLoader.cpp)
target_include_directories(StackQueueTextureLifetimeTests PRIVATE src)
target_link_libraries(StackQueueTextureLifetimeTests PRIVATE imgui_core)
if(WIN32)
    target_link_libraries(StackQueueTextureLifetimeTests PRIVATE opengl32)
elseif(APPLE)
    target_link_libraries(StackQueueTextureLifetimeTests PRIVATE "-framework OpenGL")
elseif(UNIX)
    target_link_libraries(StackQueueTextureLifetimeTests PRIVATE GL)
endif()
add_test(NAME StackQueue.TextureLifetime COMMAND StackQueueTextureLifetimeTests)
set_tests_properties(StackQueue.TextureLifetime PROPERTIES LABELS "queue;ui;gpu" TIMEOUT 30)
