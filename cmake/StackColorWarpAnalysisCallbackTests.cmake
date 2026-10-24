set(color_warp_analysis_test "${CMAKE_CURRENT_BINARY_DIR}/color_warp_analysis_callback_tests.cpp")
add_custom_command(OUTPUT "${color_warp_analysis_test}"
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tools/tests/color_warp_analysis_callback_tests.py"
        --output "${color_warp_analysis_test}"
    DEPENDS tools/tests/color_warp_analysis_callback_tests.py
        src/Editor/Internal/RawLab/EditorModuleRawLabPreview.cpp
        src/Editor/EditorModuleTypes.h
    VERBATIM)
add_executable(StackColorWarpAnalysisCallbackTests "${color_warp_analysis_test}" src/Async/TaskSystem.cpp)
target_include_directories(StackColorWarpAnalysisCallbackTests PRIVATE src)
target_link_libraries(StackColorWarpAnalysisCallbackTests PRIVATE Threads::Threads)
add_test(NAME StackEditor.ColorWarpAnalysisCallback COMMAND StackColorWarpAnalysisCallbackTests)
set_tests_properties(StackEditor.ColorWarpAnalysisCallback PROPERTIES LABELS "editor;color;async;cpu" TIMEOUT 30)
