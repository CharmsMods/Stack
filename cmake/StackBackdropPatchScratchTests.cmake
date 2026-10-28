set(backdrop_patch_scratch_test "${CMAKE_CURRENT_BINARY_DIR}/generated/backdrop_patch_scratch_tests.cpp")
add_custom_command(OUTPUT "${backdrop_patch_scratch_test}"
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tools/tests/backdrop_patch_scratch_tests.py"
        --output "${backdrop_patch_scratch_test}"
    DEPENDS tools/tests/backdrop_patch_scratch_tests.py tools/tests/backdrop_patch_scratch_tests.cpp
        src/Editor/Internal/RawLab/EditorModuleRawImageBackdrop.cpp
    VERBATIM)
add_executable(StackBackdropPatchScratchTests "${backdrop_patch_scratch_test}")
target_include_directories(StackBackdropPatchScratchTests PRIVATE src)
target_link_libraries(StackBackdropPatchScratchTests PRIVATE imgui_core)
add_test(NAME StackRenderer.BackdropPatchScratch COMMAND StackBackdropPatchScratchTests)
set_tests_properties(StackRenderer.BackdropPatchScratch PROPERTIES LABELS "renderer;ui;cpu" TIMEOUT 30)
