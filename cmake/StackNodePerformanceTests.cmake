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

set(hdr_surface_retention_test "${CMAKE_CURRENT_BINARY_DIR}/hdr_surface_retention_tests.cpp")
add_custom_command(OUTPUT "${hdr_surface_retention_test}"
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tools/tests/hdr_surface_retention_tests.py"
        --output "${hdr_surface_retention_test}"
    DEPENDS tools/tests/hdr_surface_retention_tests.py
        src/Hdr/WindowPresenter.cpp src/Hdr/WindowPresenter.h
        src/App/AppShell.cpp
    VERBATIM)
add_executable(StackHdrSurfaceRetentionTests "${hdr_surface_retention_test}")
target_include_directories(StackHdrSurfaceRetentionTests PRIVATE src)

set(viewport_result_adoption_test "${CMAKE_CURRENT_BINARY_DIR}/viewport_result_adoption_tests.cpp")
add_custom_command(OUTPUT "${viewport_result_adoption_test}"
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tools/tests/viewport_result_adoption_tests.py"
        --output "${viewport_result_adoption_test}"
    DEPENDS tools/tests/viewport_result_adoption_tests.py
        src/Editor/Internal/EditorModuleViewportPreview.cpp
        src/Editor/Internal/EditorModuleGraphRendering.cpp
    VERBATIM)
add_executable(StackViewportResultAdoptionTests "${viewport_result_adoption_test}")
target_include_directories(StackViewportResultAdoptionTests PRIVATE src)

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
add_test(NAME StackHdr.SurfaceRetention COMMAND StackHdrSurfaceRetentionTests)
add_test(NAME StackEditor.ViewportResultAdoption COMMAND StackViewportResultAdoptionTests)
add_test(NAME StackEditor.ColorWarpCloudPreparation COMMAND StackColorWarpCloudPreparationTests)
set_tests_properties(StackRaw.PreparedTonalCurve StackRenderer.CooperativeGpuScheduling
    StackHdr.CompatibleTransferPolicy StackHdr.SurfaceRetention StackEditor.ViewportResultAdoption
    StackEditor.ColorWarpCloudPreparation
    PROPERTIES LABELS "renderer;editor;performance;cpu" TIMEOUT 30)
