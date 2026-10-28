set(library_grid_motion_test "${CMAKE_CURRENT_BINARY_DIR}/generated/library_grid_motion_tests.cpp")
add_custom_command(OUTPUT "${library_grid_motion_test}"
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tools/tests/library_grid_motion_tests.py"
        --output "${library_grid_motion_test}"
    DEPENDS tools/tests/library_grid_motion_tests.py tools/tests/library_grid_motion_tests.cpp
        src/Library/LibraryModuleGrid.cpp src/Library/Internal/LibraryModuleUIHelpers.cpp
        src/Utils/ImGuiExtras.cpp
    VERBATIM)
add_executable(StackLibraryGridMotionTests "${library_grid_motion_test}")
target_include_directories(StackLibraryGridMotionTests PRIVATE src)
target_link_libraries(StackLibraryGridMotionTests PRIVATE imgui_core)
add_test(NAME StackLibrary.GridMotion COMMAND StackLibraryGridMotionTests)
set_tests_properties(StackLibrary.GridMotion PROPERTIES LABELS "library;ui;cpu" TIMEOUT 30)
