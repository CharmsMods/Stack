set(gallery_project_selection_test "${CMAKE_CURRENT_BINARY_DIR}/generated/gallery_project_selection_tests.cpp")
add_custom_command(OUTPUT "${gallery_project_selection_test}"
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tools/tests/gallery_project_selection_tests.py"
        --output "${gallery_project_selection_test}"
    DEPENDS tools/tests/gallery_project_selection_tests.py
        src/Editor/Internal/RawLab/EditorModuleRawLabGallery.cpp
    VERBATIM)
add_executable(StackGalleryProjectSelectionTests "${gallery_project_selection_test}")
target_link_libraries(StackGalleryProjectSelectionTests PRIVATE imgui_core)
add_test(NAME StackRaw.GalleryProjectSelection COMMAND StackGalleryProjectSelectionTests)
set_tests_properties(StackRaw.GalleryProjectSelection PROPERTIES
    LABELS "raw;gallery;selection;ui;cpu" TIMEOUT 30)
