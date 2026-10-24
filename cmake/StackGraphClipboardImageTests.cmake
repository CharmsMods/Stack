find_package(Python3 COMPONENTS Interpreter REQUIRED)
set(clipboard_image_harness "${CMAKE_CURRENT_BINARY_DIR}/graph_clipboard_image_tests.cpp")
add_custom_command(OUTPUT "${clipboard_image_harness}"
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tools/tests/graph_clipboard_image_tests.py"
        --output "${clipboard_image_harness}"
    DEPENDS tools/tests/graph_clipboard_image_tests.py
        src/Editor/NodeGraph/EditorNodeGraphSelectionExport.cpp
        src/Editor/NodeGraph/UI/EditorNodeGraphUIClipboard.cpp
        src/Editor/NodeGraph/EditorNodeGraphSerializer.cpp
    VERBATIM)
add_executable(StackGraphClipboardImageTests "${clipboard_image_harness}"
    src/Editor/NodeGraph/Serialization/EditorNodeGraphImageSerialization.cpp
    src/NodeMath/SourceColorMetadata.cpp
    src/NodeMath/FirstClassValue.cpp
    src/NodeMath/DescriptorSerialization.cpp
    src/NodeMath/ContractTypes.cpp
    src/NodeMath/Uuid.cpp
    src/ThirdParty/stb_image_impl.cpp)
target_include_directories(StackGraphClipboardImageTests PRIVATE src)
if(MSVC)
    target_compile_options(StackGraphClipboardImageTests PRIVATE /Gy /Gw)
    target_link_options(StackGraphClipboardImageTests PRIVATE /OPT:REF /INCREMENTAL:NO)
else()
    target_compile_options(StackGraphClipboardImageTests PRIVATE -ffunction-sections -fdata-sections)
    if(APPLE)
        target_link_options(StackGraphClipboardImageTests PRIVATE -Wl,-dead_strip)
    else()
        target_link_options(StackGraphClipboardImageTests PRIVATE -Wl,--gc-sections)
    endif()
endif()
add_test(NAME StackGraph.ClipboardImages COMMAND StackGraphClipboardImageTests)
set_tests_properties(StackGraph.ClipboardImages PROPERTIES LABELS "graph;clipboard;cpu" TIMEOUT 30)
