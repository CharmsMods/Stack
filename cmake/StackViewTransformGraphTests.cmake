set(view_transform_graph_test "${CMAKE_CURRENT_BINARY_DIR}/generated/view_transform_graph_tests.cpp")
add_custom_command(OUTPUT "${view_transform_graph_test}"
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tools/tests/view_transform_graph_tests.py"
        --output "${view_transform_graph_test}"
    DEPENDS tools/tests/view_transform_graph_tests.py tools/tests/view_transform_graph_tests.cpp
        src/Editor/Internal/RawLab/EditorModuleRawLabView.cpp
        src/Editor/Internal/EditorModuleRawWorkspaceLab.cpp src/Raw/RawDevelopmentRecipe.cpp
    VERBATIM)
add_executable(StackViewTransformGraphTests "${view_transform_graph_test}")
target_include_directories(StackViewTransformGraphTests PRIVATE src)
target_link_libraries(StackViewTransformGraphTests PRIVATE imgui_core)
add_test(NAME StackRaw.ViewTransformGraph COMMAND StackViewTransformGraphTests)
set_tests_properties(StackRaw.ViewTransformGraph PROPERTIES LABELS "raw;ui;input;cpu" TIMEOUT 30)
