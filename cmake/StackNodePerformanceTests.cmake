add_executable(StackPreparedTonalCurveTests
    tools/tests/prepared_tonal_curve_tests.cpp
    src/Raw/RawDevelopmentRecipe.cpp
    src/Raw/Tone/SceneTone.cpp
    src/Raw/Curves/LocalEvCurveNormalization.cpp
    src/Raw/RawZoneAreaRecipe.cpp
    src/Raw/RawZoneAreaGain.cpp)
target_include_directories(StackPreparedTonalCurveTests PRIVATE src)

add_executable(StackCooperativeGpuSchedulingTests
    tools/tests/cooperative_gpu_scheduling_tests.cpp)
target_include_directories(StackCooperativeGpuSchedulingTests PRIVATE src)

add_executable(StackCompatibleTransferPolicyTests
    tools/tests/compatible_transfer_policy_tests.cpp)
target_include_directories(StackCompatibleTransferPolicyTests PRIVATE src)

# Use the same production math dependencies as the existing Color Warp check.
get_target_property(STACK_COLOR_WARP_CHECK_SOURCES StackColorWarpCachePolicyTests SOURCES)
list(REMOVE_ITEM STACK_COLOR_WARP_CHECK_SOURCES tools/tests/color_warp_cache_policy_tests.cpp)
add_executable(StackColorWarpCloudPreparationTests
    tools/tests/color_warp_cloud_preparation_tests.cpp
    src/Editor/NodeEditors/ColorWarpCloudPreparation.cpp
    src/Editor/NodeEditors/ColorWarpFeedback.cpp
    ${STACK_COLOR_WARP_CHECK_SOURCES})
target_include_directories(StackColorWarpCloudPreparationTests PRIVATE
    src ${imgui_SOURCE_DIR} ${glfw_SOURCE_DIR}/include)

foreach(target StackPreparedTonalCurveTests StackColorWarpCloudPreparationTests)
    if(MSVC)
        target_compile_options(${target} PRIVATE /Gy /Gw)
        target_link_options(${target} PRIVATE /OPT:REF)
    elseif(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang" AND NOT APPLE)
        target_compile_options(${target} PRIVATE -ffunction-sections -fdata-sections)
        target_link_options(${target} PRIVATE -Wl,--gc-sections)
    endif()
endforeach()

add_test(NAME StackRaw.PreparedTonalCurve COMMAND StackPreparedTonalCurveTests)
add_test(NAME StackRenderer.CooperativeGpuScheduling COMMAND StackCooperativeGpuSchedulingTests)
add_test(NAME StackHdr.CompatibleTransferPolicy COMMAND StackCompatibleTransferPolicyTests)
add_test(NAME StackEditor.ColorWarpCloudPreparation COMMAND StackColorWarpCloudPreparationTests)
set_tests_properties(StackRaw.PreparedTonalCurve StackRenderer.CooperativeGpuScheduling
    StackHdr.CompatibleTransferPolicy StackEditor.ColorWarpCloudPreparation
    PROPERTIES LABELS "renderer;editor;performance;cpu" TIMEOUT 30)
