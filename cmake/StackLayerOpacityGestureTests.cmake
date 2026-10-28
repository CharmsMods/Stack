set(layer_opacity_gesture_test "${CMAKE_CURRENT_BINARY_DIR}/generated/layer_opacity_gesture_tests.cpp")
add_custom_command(OUTPUT "${layer_opacity_gesture_test}"
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tools/tests/layer_opacity_gesture_tests.py"
        --output "${layer_opacity_gesture_test}"
    DEPENDS tools/tests/layer_opacity_gesture_tests.py
        tools/tests/layer_opacity_gesture_tests.cpp
        src/Editor/Internal/RawLab/EditorModuleRawLayerPanel.cpp
    VERBATIM)
add_executable(StackLayerOpacityGestureTests "${layer_opacity_gesture_test}")
target_link_libraries(StackLayerOpacityGestureTests PRIVATE imgui_core)
add_test(NAME StackRaw.LayerOpacityGesture COMMAND StackLayerOpacityGestureTests)
set_tests_properties(StackRaw.LayerOpacityGesture PROPERTIES
    LABELS "raw;controls;ui;cpu" TIMEOUT 30)
