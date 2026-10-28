set(gallery_project_preview_test "${CMAKE_CURRENT_BINARY_DIR}/generated/gallery_project_preview_tests.cpp")
add_custom_command(OUTPUT "${gallery_project_preview_test}"
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tools/tests/gallery_project_preview_tests.py"
        --output "${gallery_project_preview_test}"
    DEPENDS tools/tests/gallery_project_preview_tests.py
        src/Editor/Internal/RawLab/EditorModuleRawLabGalleryWorkspace.cpp
        src/Editor/Internal/RawLab/EditorModuleRawLabFilmstripPreview.cpp
        src/Editor/Internal/RawLab/EditorModuleRawLabGallery.cpp
        src/Editor/Internal/EditorModuleRawWorkspaceLab.cpp
    VERBATIM)
add_executable(StackGalleryProjectPreviewTests "${gallery_project_preview_test}")
target_include_directories(StackGalleryProjectPreviewTests PRIVATE src)
add_test(NAME StackRaw.GalleryProjectPreview COMMAND StackGalleryProjectPreviewTests)
set_tests_properties(StackRaw.GalleryProjectPreview PROPERTIES
    LABELS "raw;gallery;preview;cpu" TIMEOUT 30)
