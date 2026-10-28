set(graph_pointer_release_test "${CMAKE_CURRENT_BINARY_DIR}/graph_pointer_release_tests.cpp")
add_custom_command(OUTPUT "${graph_pointer_release_test}"
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tools/tests/graph_pointer_release_tests.py"
        --output "${graph_pointer_release_test}"
    DEPENDS tools/tests/graph_pointer_release_tests.py
        tools/tests/graph_drag_autofocus_fixture.cpp
        src/Editor/Internal/EditorModuleView.cpp
        src/Editor/EditorModuleTypes.h
        src/Editor/NodeGraph/UI/EditorNodeGraphUIInteraction.cpp
        src/Editor/NodeGraph/UI/EditorNodeGraphUICamera.cpp
        src/Editor/NodeGraph/UI/EditorNodeGraphUICanvas.cpp
        src/Utils/ImGuiExtras.cpp
        src/Editor/NodeGraph/UI/EditorNodeGraphUIDragLayout.cpp
        src/Editor/NodeGraph/UI/EditorNodeGraphUILinksAndGroups.cpp
    VERBATIM)
add_executable(StackGraphPointerReleaseTests "${graph_pointer_release_test}")
add_test(NAME StackGraph.PointerRelease COMMAND StackGraphPointerReleaseTests)
set_tests_properties(StackGraph.PointerRelease PROPERTIES LABELS "graph;interaction;ui;cpu" TIMEOUT 30)
