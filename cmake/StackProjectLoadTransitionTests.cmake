set(project_load_transition_test "${CMAKE_CURRENT_BINARY_DIR}/generated/project_load_transition_tests.cpp")
add_custom_command(OUTPUT "${project_load_transition_test}"
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tools/tests/project_load_transition_tests.py"
        --output "${project_load_transition_test}"
    DEPENDS tools/tests/project_load_transition_tests.py tools/tests/rail_navigation_intent_tests.py
        src/App/AppShellLibraryTransition.cpp src/App/ProjectLoadTransition.h
    VERBATIM)
add_executable(StackProjectLoadTransitionTests "${project_load_transition_test}")
target_include_directories(StackProjectLoadTransitionTests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
add_test(NAME StackUi.ProjectLoadTransition COMMAND StackProjectLoadTransitionTests)
set_tests_properties(StackUi.ProjectLoadTransition PROPERTIES LABELS "ui;navigation;ownership;cpu" TIMEOUT 30)
