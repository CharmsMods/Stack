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
    src/Utils/GlassControls.cpp
    src/Renderer/GlassChrome.cpp
    src/Renderer/Glass/GlassChromeRendering.cpp
    src/Renderer/Glass/GlassChromeJoins.cpp
    src/Renderer/Glass/ProximityJoins.cpp
    src/Renderer/Glass/GlassChromeInk.cpp
    src/Renderer/Glass/MaterialRenderer.cpp
    src/Renderer/GLHelpers.cpp
    src/Renderer/GLLoader.cpp
    src/Animation/AnimationTypes.cpp
    src/Animation/ScalarChannel.cpp
    src/Animation/ControlMotion.cpp
    src/Notifications/NotificationPreferences.cpp
    src/Utils/MorphingLabel.cpp
    src/ThirdParty/imstb_truetype_impl.cpp
    src/Editor/NodeGraph/UI/NodeSocketGlyphs.cpp)
target_include_directories(StackGraphControlTests PRIVATE src src/ThirdParty "${CMAKE_BINARY_DIR}/generated/src")
target_link_libraries(StackGraphControlTests PRIVATE imgui_core)
if(WIN32)
    target_link_libraries(StackGraphControlTests PRIVATE opengl32)
elseif(APPLE)
    target_link_libraries(StackGraphControlTests PRIVATE "-framework OpenGL")
elseif(UNIX)
    target_link_libraries(StackGraphControlTests PRIVATE GL)
endif()
add_test(NAME StackEditor.GraphControls COMMAND StackGraphControlTests)
set_tests_properties(StackEditor.GraphControls PROPERTIES LABELS "editor;node-graph;interaction")
add_test(NAME StackEditor.NodeNotesTransitions COMMAND StackGraphControlTests --node-notes)
set_tests_properties(StackEditor.NodeNotesTransitions PROPERTIES LABELS "editor;node-graph;interaction")
add_test(NAME StackEditor.GraphNumericOwnership COMMAND StackGraphControlTests --numeric-ownership)
set_tests_properties(StackEditor.GraphNumericOwnership PROPERTIES LABELS "editor;node-graph;interaction")
add_test(NAME StackEditor.NodeNotesCacheOwnership
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tools/tests/node_notes_cache_wiring_tests.py")
set_tests_properties(StackEditor.NodeNotesCacheOwnership PROPERTIES LABELS "editor;node-graph;interaction")
