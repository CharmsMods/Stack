set(rail_navigation_intent_test "${CMAKE_CURRENT_BINARY_DIR}/generated/rail_navigation_intent_tests.cpp")
add_custom_command(OUTPUT "${rail_navigation_intent_test}"
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tools/tests/rail_navigation_intent_tests.py"
        --output "${rail_navigation_intent_test}"
    DEPENDS tools/tests/rail_navigation_intent_tests.py
        src/App/AppNavigationRail.cpp src/App/AppProjectWorkspaces.cpp src/App/AppShell.cpp
    VERBATIM)
add_executable(StackRailNavigationIntentTests "${rail_navigation_intent_test}")
target_include_directories(StackRailNavigationIntentTests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
add_test(NAME StackUi.RailNavigationIntent COMMAND StackRailNavigationIntentTests)
set_tests_properties(StackUi.RailNavigationIntent PROPERTIES LABELS "ui;navigation;ownership;cpu" TIMEOUT 30)
