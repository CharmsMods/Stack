set(subject_map_reference_test "${CMAKE_CURRENT_BINARY_DIR}/generated/subject_map_reference_tests.cpp")
add_custom_command(OUTPUT "${subject_map_reference_test}"
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tools/tests/subject_map_reference_tests.py"
        --output "${subject_map_reference_test}"
    DEPENDS tools/tests/subject_map_reference_tests.py tools/tests/subject_map_reference_tests.cpp
        src/Editor/Internal/EditorModuleDevelopSubjectImportance.cpp src/Editor/EditorModuleTypes.h
    VERBATIM)
add_executable(StackSubjectMapReferenceTests "${subject_map_reference_test}")
target_include_directories(StackSubjectMapReferenceTests PRIVATE src)
add_test(NAME StackEditor.SubjectMapReference COMMAND StackSubjectMapReferenceTests)
set_tests_properties(StackEditor.SubjectMapReference PROPERTIES LABELS "viewport;ui;cpu" TIMEOUT 30)
