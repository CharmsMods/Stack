set(image_storage_cpu_sources
    src/Editor/NodeGraph/EditorNodeGraph.cpp
    src/Project/GraphImagePayload.cpp
    src/NodeMath/FirstClassValue.cpp
    src/NodeMath/DescriptorSerialization.cpp
    src/NodeMath/ContractTypes.cpp)
add_executable(StackImagePayloadByteStorageTests
    tools/tests/image_payload_byte_storage_tests.cpp
    ${image_storage_cpu_sources}
    src/Graph/GraphDocumentRules.cpp
    src/Editor/NodeGraph/Model/EditorNodeGraphMutation.cpp
    src/Editor/NodeGraph/Serialization/EditorNodeGraphImageSerialization.cpp
    src/NodeMath/SourceColorMetadata.cpp
    src/ThirdParty/stb_image_impl.cpp)
find_package(Python3 COMPONENTS Interpreter REQUIRED)
set(image_storage_capture "${CMAKE_CURRENT_BINARY_DIR}/image_payload_storage_capture_tests.cpp")
add_custom_command(OUTPUT "${image_storage_capture}"
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tools/tests/image_payload_storage_capture_tests.py"
        --output "${image_storage_capture}"
    DEPENDS tools/tests/image_payload_storage_capture_tests.py
        src/Editor/Internal/EditorModuleGraphHistory.cpp
        src/Editor/Internal/EditorModuleGraphImageNodes.cpp
        src/Editor/Internal/EditorModulePersistence.cpp
        src/Editor/NodeGraph/Serialization/EditorNodeGraphImageSerialization.cpp
        src/Editor/NodeGraph/EditorNodeGraphCompound.cpp
    VERBATIM)
add_executable(StackImagePayloadStorageCaptureTests "${image_storage_capture}"
    ${image_storage_cpu_sources} src/Editor/NodeGraph/EditorNodeGraphCompound.cpp)
foreach(target StackImagePayloadByteStorageTests StackImagePayloadStorageCaptureTests)
    target_include_directories(${target} PRIVATE src)
    if(MSVC)
        target_compile_options(${target} PRIVATE /Gy /Gw)
        target_link_options(${target} PRIVATE /OPT:REF /INCREMENTAL:NO)
    else()
        target_compile_options(${target} PRIVATE -ffunction-sections -fdata-sections)
        if(APPLE)
            target_link_options(${target} PRIVATE -Wl,-dead_strip)
        else()
            target_link_options(${target} PRIVATE -Wl,--gc-sections)
        endif()
    endif()
endforeach()
add_test(NAME StackProject.ImagePayloadByteStorage COMMAND StackImagePayloadByteStorageTests)
add_test(NAME StackProject.ImagePayloadStorageCapture COMMAND StackImagePayloadStorageCaptureTests)
set_tests_properties(StackProject.ImagePayloadByteStorage StackProject.ImagePayloadStorageCapture
    PROPERTIES LABELS "project;history;image;cpu" TIMEOUT 30)
