add_executable(StackColorOrientationTests
    tools/tests/color_orientation_tests.cpp
    src/Raw/Bracketing/Recipe.cpp)
target_include_directories(StackColorOrientationTests PRIVATE src src/ThirdParty)
target_link_libraries(StackColorOrientationTests PRIVATE imgui_core)
add_test(NAME StackEditor.ColorOrientation COMMAND StackColorOrientationTests)
set_tests_properties(StackEditor.ColorOrientation PROPERTIES LABELS "editor;color;bracketing;cpu")
