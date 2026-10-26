add_executable(StackRawZoneAreaTests
    src/Raw/RawZoneAreaRasterizer.cpp
    src/Editor/RawZoneAreaPreview.cpp
    src/Async/TaskSystem.cpp
    src/Editor/Internal/RawLab/RawLabAreaImageInteraction.cpp
    src/Editor/Internal/RawLab/RawLabAreaImageOverlay.cpp
    tools/tests/raw_zone_interaction_tests.cpp
    tools/tests/raw_zone_area_tests.cpp
    tools/tests/raw_zone_guidance_tests.cpp
    src/App/Validation/Suites/RawZoneAreaGraphValidation.cpp
    src/Editor/Internal/RawLab/RawLabAreaGraph.cpp
    src/Editor/Internal/RawLab/RawLabAreaMask.cpp
    src/Editor/Internal/RawLab/RawLabCurveEditor.cpp
    src/Editor/GraphUI/GraphPlotStyle.cpp
    src/Utils/GlassControls.cpp
    src/Renderer/GlassChrome.cpp
    src/Renderer/Glass/GlassChromeRendering.cpp
    src/Renderer/Glass/GlassChromeJoins.cpp
    src/Renderer/Glass/ProximityJoins.cpp
    src/Renderer/Glass/GlassChromeInk.cpp
    src/Renderer/Glass/MaterialRenderer.cpp
    src/Animation/AnimationTypes.cpp
    src/Animation/ScalarChannel.cpp
    src/Animation/ControlMotion.cpp
    src/Notifications/NotificationPreferences.cpp
    src/Raw/RawZoneAreaRecipe.cpp
    src/Raw/RawZoneAreaMask.cpp
    src/Raw/RawZoneAreaGuidance.cpp
    src/Raw/ImageGuidance.cpp
    src/Raw/RawZoneAreaGain.cpp
    src/Raw/RawDevelopmentRecipe.cpp
    src/Raw/RawGraphOperation.cpp
    src/NodeMath/ToneCurveParameters.cpp
    src/NodeMath/ContractTypes.cpp
    src/Raw/Tone/SceneTone.cpp
    src/Raw/Detail/DetailContrast.cpp
    src/Raw/RawColorCalibration.cpp
    src/Raw/RawEditAttributes.cpp
    src/Raw/RawImageData.cpp
    src/Raw/RawProcessingMath.cpp
    src/Raw/Denoise/RawDenoiseControlMap.cpp
    src/Renderer/Internal/RawZoneAreaRenderer.cpp
    src/Renderer/Internal/RawZoneAreaGuide.cpp
    src/Renderer/GLHelpers.cpp
    src/Renderer/GLLoader.cpp)
target_include_directories(StackRawZoneAreaTests PRIVATE src "${CMAKE_BINARY_DIR}/generated/src")
target_link_libraries(StackRawZoneAreaTests PRIVATE glfw imgui_core)
if(WIN32)
    target_link_libraries(StackRawZoneAreaTests PRIVATE opengl32)
elseif(APPLE)
    target_link_libraries(StackRawZoneAreaTests PRIVATE "-framework OpenGL")
else()
    target_link_libraries(StackRawZoneAreaTests PRIVATE GL)
endif()
add_test(NAME StackRaw.ZoneAreas COMMAND StackRawZoneAreaTests)
set_tests_properties(StackRaw.ZoneAreas PROPERTIES LABELS "raw;zones;gpu")

add_test(NAME StackRaw.ZoneAreasPipeline COMMAND Stack --validate-raw-zone-areas)
set_tests_properties(StackRaw.ZoneAreasPipeline PROPERTIES LABELS "raw;zones;gpu")
