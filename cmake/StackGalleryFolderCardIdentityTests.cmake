# The fixture includes the complete production renderer while recording native
# ImGui hit items; material/theme doubles avoid a GL or application dependency.
add_executable(StackGalleryFolderCardIdentityTests tools/tests/gallery_folder_card_identity_tests.cpp)
target_include_directories(StackGalleryFolderCardIdentityTests PRIVATE src)
target_link_libraries(StackGalleryFolderCardIdentityTests PRIVATE imgui_core)
add_test(NAME StackRaw.GalleryFolderCardIdentity COMMAND StackGalleryFolderCardIdentityTests)
set_tests_properties(StackRaw.GalleryFolderCardIdentity PROPERTIES
    LABELS "raw;gallery;identity;ui;cpu" TIMEOUT 30)
