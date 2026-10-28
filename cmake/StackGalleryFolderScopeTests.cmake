add_executable(StackGalleryFolderScopeTests
    tools/tests/gallery_folder_scope_tests.cpp
    src/Raw/RawSourceFolders.cpp
    src/Editor/Internal/RawLab/RawLabGalleryFolderTree.cpp)
target_include_directories(StackGalleryFolderScopeTests PRIVATE src)
add_test(NAME StackRaw.GalleryFolderScope COMMAND StackGalleryFolderScopeTests)
set_tests_properties(StackRaw.GalleryFolderScope PROPERTIES
    LABELS "raw;gallery;folders;cpu" TIMEOUT 30)
