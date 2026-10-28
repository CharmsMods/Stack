set(gallery_navigator_test "${CMAKE_CURRENT_BINARY_DIR}/generated/gallery_navigator_tests.cpp")
add_custom_command(OUTPUT "${gallery_navigator_test}"
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tools/tests/gallery_navigator_tests.py"
        --output "${gallery_navigator_test}"
    DEPENDS tools/tests/gallery_navigator_tests.py tools/tests/gallery_navigator_tests.cpp
        src/Editor/Internal/RawLab/RawLabGalleryNavigator.cpp
    VERBATIM)
add_executable(StackGalleryNavigatorTests "${gallery_navigator_test}"
    src/Editor/Internal/RawLab/RawLabGalleryFolderTree.cpp)
target_include_directories(StackGalleryNavigatorTests PRIVATE src src/Editor/Internal/RawLab)
target_link_libraries(StackGalleryNavigatorTests PRIVATE imgui_core)
add_test(NAME StackRaw.GalleryNavigator COMMAND StackGalleryNavigatorTests)
set_tests_properties(StackRaw.GalleryNavigator PROPERTIES LABELS "raw;gallery;identity;ui;cpu" TIMEOUT 30)

if(MSVC)
    target_compile_options(StackGalleryNavigatorTests PRIVATE /utf-8)
endif()
