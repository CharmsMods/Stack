add_executable(StackGraphControlTests
    tools/tests/graph_control_tests.cpp
    tools/tests/graph_cursor_tests.cpp
    tools/tests/floating_slider_tests.cpp
    tools/tests/floating_surface_tests.cpp
    tools/tests/node_notes_interaction_tests.cpp
    tools/tests/graph_numeric_ownership_tests.cpp
    tools/tests/graph_numeric_disabled_tests.cpp
    src/Editor/NodeGraph/UI/NodeNotesView.cpp
    src/NodeMath/FirstClassValue.cpp
    src/NodeMath/DescriptorSerialization.cpp
    src/NodeMath/ContractTypes.cpp
    src/Utils/FloatingSurfaceInput.cpp
    src/Editor/Internal/RawLab/RawCalibrationEditor.cpp
    src/Raw/RawColorCalibration.cpp
    src/App/GraphNativeCursor.cpp
    src/Editor/NodeGraph/UI/ContinuousLinkStroke.cpp
    src/Editor/NodeGraph/UI/NodeSurfaceDrawing.cpp
    src/App/settings/CreamPalette.cpp
    src/App/settings/PrimaryAction.cpp
    src/Utils/ImGuiExtras.cpp
    src/Utils/GraphCursor.cpp
    src/Utils/GraphNumericControls.cpp
    src/Utils/FloatingSlider.cpp
    src/Utils/MorphingLabel.cpp
    src/ThirdParty/imstb_truetype_impl.cpp
    src/Editor/NodeGraph/UI/NodeSocketGlyphs.cpp)
target_include_directories(StackGraphControlTests PRIVATE src src/ThirdParty)
target_link_libraries(StackGraphControlTests PRIVATE imgui_core)
if(WIN32)
    target_link_libraries(StackGraphControlTests PRIVATE opengl32)
endif()
add_test(NAME StackEditor.GraphControls COMMAND StackGraphControlTests)
set_tests_properties(StackEditor.GraphControls PROPERTIES LABELS "editor;node-graph;interaction")
add_test(NAME StackEditor.NodeNotesTransitions COMMAND StackGraphControlTests --node-notes)
set_tests_properties(StackEditor.NodeNotesTransitions PROPERTIES LABELS "editor;node-graph;interaction")
add_test(NAME StackEditor.GraphNumericOwnership COMMAND StackGraphControlTests --numeric-ownership)
set_tests_properties(StackEditor.GraphNumericOwnership PROPERTIES LABELS "editor;node-graph;interaction")
