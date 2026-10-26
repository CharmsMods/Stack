set(library_refresh_lifetime_test "${CMAKE_CURRENT_BINARY_DIR}/library_refresh_lifetime_tests.cpp")
add_custom_command(OUTPUT "${library_refresh_lifetime_test}"
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tools/tests/library_refresh_lifetime_tests.py"
        --output "${library_refresh_lifetime_test}"
    DEPENDS tools/tests/library_refresh_lifetime_tests.py
        src/Library/ProjectData.h
        src/Library/LibraryManager.cpp
        src/Library/LibraryManagerRefresh.cpp
        src/Library/LibraryManagerThumbnails.cpp
        src/Library/LibraryModulePreviews.cpp
    VERBATIM)
add_executable(StackLibraryRefreshLifetimeTests "${library_refresh_lifetime_test}")
target_include_directories(StackLibraryRefreshLifetimeTests PRIVATE src)
target_link_libraries(StackLibraryRefreshLifetimeTests PRIVATE Threads::Threads)
add_test(NAME StackLibrary.RefreshLifetime COMMAND StackLibraryRefreshLifetimeTests)
set_tests_properties(StackLibrary.RefreshLifetime PROPERTIES LABELS "library;render;async;cpu" TIMEOUT 30)
