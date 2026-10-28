set(library_tag_filter_test "${CMAKE_CURRENT_BINARY_DIR}/library_tag_filter_tests.cpp")
add_custom_command(OUTPUT "${library_tag_filter_test}"
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tools/tests/library_tag_filter_tests.py"
        --output "${library_tag_filter_test}"
    DEPENDS tools/tests/library_tag_filter_tests.py
        src/Library/LibraryModuleTags.cpp
    VERBATIM)
add_executable(StackLibraryTagFilterTests "${library_tag_filter_test}" src/Library/TagManager.cpp)
target_include_directories(StackLibraryTagFilterTests PRIVATE src)
target_link_libraries(StackLibraryTagFilterTests PRIVATE imgui_core)
add_test(NAME StackLibrary.TagFilters COMMAND StackLibraryTagFilterTests)
set_tests_properties(StackLibrary.TagFilters PROPERTIES LABELS "library;filter;ui;cpu" TIMEOUT 30)
