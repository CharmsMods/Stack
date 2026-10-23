# Focused processing checks; the desktop still links only burst contracts.
add_executable(StackBurstQualityTests EXCLUDE_FROM_ALL
    tools/tests/burst_quality_tests.cpp
    src/Raw/Bracketing/SuperResolution/Kernel.cpp
    src/Raw/Bracketing/SuperResolution/KernelGpu.cpp
    src/Renderer/GLLoader.cpp
    src/Renderer/GLHelpers.cpp)
target_include_directories(StackBurstQualityTests PRIVATE src)
target_link_libraries(StackBurstQualityTests PRIVATE glfw)
if(WIN32)
    target_link_libraries(StackBurstQualityTests PRIVATE opengl32)
elseif(APPLE)
    target_link_libraries(StackBurstQualityTests PRIVATE "-framework OpenGL")
else()
    target_link_libraries(StackBurstQualityTests PRIVATE GL)
endif()
add_test(NAME StackBurst.Quality COMMAND StackBurstQualityTests)
set_tests_properties(StackBurst.Quality PROPERTIES LABELS "burst;calibration;super-resolution;gpu")
