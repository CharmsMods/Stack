add_executable(StackGraphControlTests
    tools/tests/graph_control_tests.cpp
    tools/tests/graph_cursor_tests.cpp
    src/App/GraphNativeCursor.cpp
    src/Editor/NodeGraph/UI/ContinuousLinkStroke.cpp
    src/Editor/NodeGraph/UI/NodeSurfaceDrawing.cpp
    src/App/settings/CreamPalette.cpp
    src/App/settings/PrimaryAction.cpp
    src/Utils/ImGuiExtras.cpp
    src/Utils/GraphCursor.cpp
    src/Utils/GraphNumericControls.cpp
    src/Editor/NodeGraph/UI/NodeSocketGlyphs.cpp)
target_include_directories(StackGraphControlTests PRIVATE src src/ThirdParty)
target_link_libraries(StackGraphControlTests PRIVATE imgui_core)
if(WIN32)
    target_link_libraries(StackGraphControlTests PRIVATE opengl32)
endif()
add_test(NAME StackEditor.GraphControls COMMAND StackGraphControlTests)
set_tests_properties(StackEditor.GraphControls PROPERTIES LABELS "editor;node-graph;interaction")
