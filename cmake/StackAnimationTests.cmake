# Dependency-free presentation contracts; no GPU, window or saved user data.
set(STACK_ANIMATION_CORE_SOURCES
    src/Animation/AnimationTypes.cpp
    src/Animation/ScalarChannel.cpp
    src/Animation/AnimationController.cpp)
add_executable(StackAnimationCoreTests
    tools/tests/animation_core_tests.cpp ${STACK_ANIMATION_CORE_SOURCES})
target_include_directories(StackAnimationCoreTests PRIVATE src)
add_test(NAME StackAnimation.Core COMMAND StackAnimationCoreTests)
set_tests_properties(StackAnimation.Core PROPERTIES LABELS "ui;animation;cpu" TIMEOUT 30)
add_executable(StackAnimationControlTests
    tools/tests/animation_control_tests.cpp
    src/Animation/ControlMotion.cpp ${STACK_ANIMATION_CORE_SOURCES})
target_include_directories(StackAnimationControlTests PRIVATE src)
add_test(NAME StackAnimation.Controls COMMAND StackAnimationControlTests)
set_tests_properties(StackAnimation.Controls PROPERTIES LABELS "ui;animation;cpu" TIMEOUT 30)
