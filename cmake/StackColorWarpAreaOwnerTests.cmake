set(color_warp_area_owner_test "${CMAKE_CURRENT_BINARY_DIR}/color_warp_area_owner_tests.cpp")
add_custom_command(OUTPUT "${color_warp_area_owner_test}"
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tools/tests/color_warp_area_owner_tests.py"
        --output "${color_warp_area_owner_test}"
    DEPENDS tools/tests/color_warp_area_owner_tests.py
        src/Editor/EditorModuleTypes.h
        src/Editor/NodeEditors/EditorModuleNodeSelection.cpp
        src/Editor/Internal/Project/EditorModuleProjectInteraction.cpp
        src/Editor/Internal/RawLab/EditorModuleRawLabColor.cpp
        src/Editor/Internal/RawLab/EditorModuleRawLabPreview.cpp
        src/Editor/Internal/RawLab/EditorModuleRawProjectGraph.cpp
        src/Editor/Internal/RawLab/RawLabColorSurface.cpp
        src/Editor/Internal/RawWorkspace/EditorModuleRawWorkspacePreview.cpp
    VERBATIM)
add_executable(StackColorWarpAreaOwnerTests "${color_warp_area_owner_test}" src/Async/TaskSystem.cpp)
target_include_directories(StackColorWarpAreaOwnerTests PRIVATE src)
target_link_libraries(StackColorWarpAreaOwnerTests PRIVATE Threads::Threads)
add_test(NAME StackEditor.ColorWarpAreaOwner COMMAND StackColorWarpAreaOwnerTests)
set_tests_properties(StackEditor.ColorWarpAreaOwner PROPERTIES LABELS "editor;color;async;cpu" TIMEOUT 30)
