add_executable(StackLayerParameterPreflightTests
    tools/tests/layer_parameter_preflight_tests.cpp
    src/Editor/Layers/SplitAdjustmentsLayers.cpp
    src/Graph/LayerOperationParameters.cpp
    src/Renderer/GLLoader.cpp)
target_include_directories(StackLayerParameterPreflightTests PRIVATE src)
target_compile_definitions(StackLayerParameterPreflightTests PRIVATE GL_GLEXT_LEGACY)
target_link_libraries(StackLayerParameterPreflightTests PRIVATE imgui_core glfw)
if(MSVC)
    target_compile_options(StackLayerParameterPreflightTests PRIVATE /Gy /Gw)
    target_link_options(StackLayerParameterPreflightTests PRIVATE /OPT:REF)
else()
    target_compile_options(StackLayerParameterPreflightTests PRIVATE -ffunction-sections -fdata-sections)
    if(APPLE)
        target_link_options(StackLayerParameterPreflightTests PRIVATE -Wl,-dead_strip)
    else()
        target_link_options(StackLayerParameterPreflightTests PRIVATE -Wl,--gc-sections)
    endif()
endif()
if(WIN32)
    target_link_libraries(StackLayerParameterPreflightTests PRIVATE opengl32)
endif()
add_test(NAME StackProject.LayerParameterPreflight COMMAND StackLayerParameterPreflightTests)
set_tests_properties(StackProject.LayerParameterPreflight PROPERTIES LABELS "project;load;cpu" TIMEOUT 30)
