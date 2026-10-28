set(library_project_card_test "${CMAKE_CURRENT_BINARY_DIR}/library_project_card_identity_tests.cpp")
add_custom_command(OUTPUT "${library_project_card_test}"
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tools/tests/library_project_card_identity_tests.py"
        --output "${library_project_card_test}"
    DEPENDS tools/tests/library_project_card_identity_tests.py
        src/Library/LibraryModuleGrid.cpp
        src/Library/LibraryModuleCards.cpp
    VERBATIM)
add_executable(StackLibraryProjectCardIdentityTests "${library_project_card_test}")
target_link_libraries(StackLibraryProjectCardIdentityTests PRIVATE imgui_core)
add_test(NAME StackLibrary.ProjectCardIdentity COMMAND StackLibraryProjectCardIdentityTests)
set_tests_properties(StackLibrary.ProjectCardIdentity PROPERTIES LABELS "library;identity;ui;cpu" TIMEOUT 30)
