set(preset_preview_warmup_test "${CMAKE_CURRENT_BINARY_DIR}/generated/preset_preview_warmup_tests.cpp")
add_custom_command(OUTPUT "${preset_preview_warmup_test}"
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tools/tests/preset_preview_warmup_tests.py"
        --output "${preset_preview_warmup_test}"
    DEPENDS tools/tests/preset_preview_warmup_tests.py
        src/Presets/PresetManager.cpp src/Presets/PresetManager.h
        src/Editor/NodeGraph/EditorNodeGraphUI.h
        src/Editor/NodeGraph/UI/EditorNodeGraphUIPresetsDrawer.cpp
    VERBATIM)
add_executable(StackPresetPreviewWarmupTests "${preset_preview_warmup_test}")
target_include_directories(StackPresetPreviewWarmupTests PRIVATE src)
add_test(NAME StackEditor.PresetPreviewWarmup COMMAND StackPresetPreviewWarmupTests)
set_tests_properties(StackEditor.PresetPreviewWarmup PROPERTIES LABELS "editor;presets;cache;cpu" TIMEOUT 30)
