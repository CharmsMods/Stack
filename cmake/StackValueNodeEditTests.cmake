set(value_node_edit_test "${CMAKE_CURRENT_BINARY_DIR}/value_node_edit_tests.cpp")
add_custom_command(OUTPUT "${value_node_edit_test}"
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tools/tests/value_node_edit_tests.py"
        --output "${value_node_edit_test}"
    DEPENDS tools/tests/value_node_edit_tests.py
        src/Editor/NodeGraph/UI/EditorNodeGraphUINodes.cpp
        src/Editor/Internal/EditorModuleGraphHistory.cpp
        src/Editor/Internal/RawLab/EditorModuleRawLayerGraph.cpp
    VERBATIM)
add_executable(StackValueNodeEditTests "${value_node_edit_test}"
    src/Editor/NodeGraph/EditorNodeGraph.cpp
    src/Editor/NodeGraph/Model/EditorNodeGraphLayoutValidation.cpp
    src/NodeMath/FirstClassValue.cpp
    src/NodeMath/DescriptorSerialization.cpp
    src/NodeMath/ContractTypes.cpp
    src/NodeMath/CompoundDefinition.cpp)
target_include_directories(StackValueNodeEditTests PRIVATE src "${imgui_SOURCE_DIR}")
if(MSVC)
    target_compile_options(StackValueNodeEditTests PRIVATE /Gy /Gw)
    target_link_options(StackValueNodeEditTests PRIVATE /OPT:REF /INCREMENTAL:NO)
else()
    target_compile_options(StackValueNodeEditTests PRIVATE -ffunction-sections -fdata-sections)
    if(APPLE)
        target_link_options(StackValueNodeEditTests PRIVATE -Wl,-dead_strip)
    else()
        target_link_options(StackValueNodeEditTests PRIVATE -Wl,--gc-sections)
    endif()
endif()
add_test(NAME StackGraph.ValueNodeEdit COMMAND StackValueNodeEditTests)
set_tests_properties(StackGraph.ValueNodeEdit PROPERTIES LABELS "graph;history;ui;cpu" TIMEOUT 30)
