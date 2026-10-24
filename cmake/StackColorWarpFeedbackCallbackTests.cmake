set(color_warp_feedback_test "${CMAKE_CURRENT_BINARY_DIR}/color_warp_feedback_callback_tests.cpp")
add_custom_command(OUTPUT "${color_warp_feedback_test}"
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tools/tests/color_warp_feedback_callback_tests.py"
        --output "${color_warp_feedback_test}"
    DEPENDS tools/tests/color_warp_feedback_callback_tests.py
        src/Editor/NodeEditors/ColorWarpFeedback.h
        src/Editor/NodeEditors/ColorWarpFeedback.cpp
    VERBATIM)
add_executable(StackColorWarpFeedbackCallbackTests "${color_warp_feedback_test}" src/Async/TaskSystem.cpp)
target_include_directories(StackColorWarpFeedbackCallbackTests PRIVATE src)
target_link_libraries(StackColorWarpFeedbackCallbackTests PRIVATE Threads::Threads)
add_test(NAME StackEditor.ColorWarpFeedbackCallback COMMAND StackColorWarpFeedbackCallbackTests)
set_tests_properties(StackEditor.ColorWarpFeedbackCallback PROPERTIES LABELS "editor;color;async;cpu" TIMEOUT 30)
