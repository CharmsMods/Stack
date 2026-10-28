set(gallery_widget_identity_test "${CMAKE_CURRENT_BINARY_DIR}/generated/gallery_widget_identity_tests.cpp")
add_custom_command(OUTPUT "${gallery_widget_identity_test}"
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tools/tests/gallery_widget_identity_tests.py"
        --output "${gallery_widget_identity_test}"
    DEPENDS tools/tests/gallery_widget_identity_tests.py
        tools/tests/gallery_widget_identity_tests.cpp
        src/Editor/Internal/RawLab/EditorModuleRawLabGallery.cpp
    VERBATIM)
add_executable(StackGalleryWidgetIdentityTests "${gallery_widget_identity_test}")
target_include_directories(StackGalleryWidgetIdentityTests PRIVATE src)
target_link_libraries(StackGalleryWidgetIdentityTests PRIVATE imgui_core)
add_test(NAME StackRaw.GalleryWidgetIdentity COMMAND StackGalleryWidgetIdentityTests)
set_tests_properties(StackRaw.GalleryWidgetIdentity PROPERTIES LABELS "raw;gallery;ui;cpu" TIMEOUT 30)
