add_executable(StackGalleryStorageTests
    tools/tests/gallery_storage_tests.cpp
    src/Raw/GalleryTileCache.cpp
    src/NodeMath/ContractTypes.cpp
    src/ThirdParty/stb_image_impl.cpp
    src/Async/TaskSystem.cpp)
target_include_directories(StackGalleryStorageTests PRIVATE src)
add_test(NAME StackRaw.GalleryStorage COMMAND StackGalleryStorageTests)
set_tests_properties(StackRaw.GalleryStorage PROPERTIES LABELS "raw;gallery;storage;cpu" TIMEOUT 30)
