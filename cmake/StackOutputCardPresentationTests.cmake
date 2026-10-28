set(output_card_test "${CMAKE_CURRENT_BINARY_DIR}/output_card_presentation_tests.cpp")
add_custom_command(OUTPUT "${output_card_test}"
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tools/tests/output_card_presentation_tests.py"
        --output "${output_card_test}"
    DEPENDS tools/tests/output_card_presentation_tests.py
        src/Editor/NodeGraph/UI/EditorNodeGraphUINodes.cpp
        src/Editor/NodeGraph/UI/EditorNodeGraphUIContextMenu.cpp
        src/Editor/NodeGraph/UI/OutputCardPresentation.h
        src/Editor/EditorModule.h
        src/Editor/Internal/EditorModuleGraphSnapshot.cpp
        src/Editor/Internal/EditorModuleRenderPresentationState.cpp
        src/Editor/NodeGraph/Model/EditorNodeGraphTraversal.cpp
        src/Editor/NodeGraph/Model/EditorNodeGraphMutation.cpp
    VERBATIM)
add_executable(StackOutputCardPresentationTests "${output_card_test}")
target_include_directories(StackOutputCardPresentationTests PRIVATE src)
add_test(NAME StackGraph.OutputCardPresentation COMMAND StackOutputCardPresentationTests)
set_tests_properties(StackGraph.OutputCardPresentation PROPERTIES LABELS "graph;ui;identity;cpu" TIMEOUT 30)
