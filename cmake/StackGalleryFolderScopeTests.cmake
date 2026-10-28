set(gallery_stack_folder_scope_fixture "${CMAKE_CURRENT_BINARY_DIR}/generated/gallery_stack_folder_scope_fixture.h")
add_custom_command(OUTPUT "${gallery_stack_folder_scope_fixture}"
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tools/tests/gallery_stack_folder_scope_fixture.py"
        --output "${gallery_stack_folder_scope_fixture}"
    DEPENDS tools/tests/gallery_stack_folder_scope_fixture.py
        src/Editor/Internal/RawLab/EditorModuleRawLabGalleryStacks.cpp
    VERBATIM)
add_executable(StackGalleryFolderScopeTests
    "${gallery_stack_folder_scope_fixture}"
    tools/tests/gallery_folder_scope_tests.cpp
    src/Raw/RawSourceFolders.cpp
    src/Editor/Internal/RawLab/RawLabGalleryFolderTree.cpp)
target_include_directories(StackGalleryFolderScopeTests PRIVATE src "${CMAKE_CURRENT_BINARY_DIR}/generated")
add_test(NAME StackRaw.GalleryFolderScope COMMAND StackGalleryFolderScopeTests)
set_tests_properties(StackRaw.GalleryFolderScope PROPERTIES
    LABELS "raw;gallery;folders;cpu" TIMEOUT 30)
