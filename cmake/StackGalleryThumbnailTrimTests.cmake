set(gallery_thumbnail_trim_test "${CMAKE_CURRENT_BINARY_DIR}/generated/gallery_thumbnail_trim_tests.cpp")
add_custom_command(OUTPUT "${gallery_thumbnail_trim_test}"
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tools/tests/gallery_thumbnail_trim_tests.py"
        --output "${gallery_thumbnail_trim_test}"
    DEPENDS tools/tests/gallery_thumbnail_trim_tests.py tools/tests/gallery_thumbnail_trim_tests.cpp
        src/Editor/Internal/RawWorkspace/EditorModuleRawWorkspaceThumbnails.cpp src/Editor/EditorModule.h
    VERBATIM)
add_executable(StackGalleryThumbnailTrimTests "${gallery_thumbnail_trim_test}")
add_test(NAME StackRaw.GalleryThumbnailTrim COMMAND StackGalleryThumbnailTrimTests)
set_tests_properties(StackRaw.GalleryThumbnailTrim PROPERTIES LABELS "raw;gallery;ui;cpu" TIMEOUT 30)
