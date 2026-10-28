set(color_warp_shortcut_test "${CMAKE_CURRENT_BINARY_DIR}/generated/color_warp_shortcut_tests.cpp")
add_custom_command(OUTPUT "${color_warp_shortcut_test}"
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tools/tests/color_warp_shortcut_tests.py"
        --output "${color_warp_shortcut_test}"
    DEPENDS tools/tests/color_warp_shortcut_tests.py
        tools/tests/color_warp_shortcut_tests.cpp
        src/Editor/Internal/RawLab/RawLabColorSurface.cpp
    VERBATIM)
add_executable(StackColorWarpShortcutTests "${color_warp_shortcut_test}")
target_link_libraries(StackColorWarpShortcutTests PRIVATE imgui_core)
add_test(NAME StackRaw.ColorWarpShortcut COMMAND StackColorWarpShortcutTests)
set_tests_properties(StackRaw.ColorWarpShortcut PROPERTIES
    LABELS "raw;controls;ui;cpu" TIMEOUT 30)
