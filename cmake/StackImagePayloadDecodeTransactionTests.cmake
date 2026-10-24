add_executable(StackImagePayloadDecodeTransactionTests
    tools/tests/image_payload_decode_transaction_tests.cpp
    src/Editor/NodeGraph/Serialization/EditorNodeGraphImageSerialization.cpp
    src/NodeMath/SourceColorMetadata.cpp
    src/NodeMath/DescriptorSerialization.cpp
    src/NodeMath/ContractTypes.cpp
    src/ThirdParty/stb_image_impl.cpp)
target_include_directories(StackImagePayloadDecodeTransactionTests PRIVATE src)
# Exercise actual PNG decoding and transactional publication, without unrelated
# graph serialization entry points and their UI/rendering dependencies.
if(MSVC)
    target_compile_options(StackImagePayloadDecodeTransactionTests PRIVATE /Gy /Gw)
    target_link_options(StackImagePayloadDecodeTransactionTests PRIVATE /OPT:REF /INCREMENTAL:NO)
else()
    target_compile_options(StackImagePayloadDecodeTransactionTests PRIVATE -ffunction-sections -fdata-sections)
    if(APPLE)
        target_link_options(StackImagePayloadDecodeTransactionTests PRIVATE -Wl,-dead_strip)
    else()
        target_link_options(StackImagePayloadDecodeTransactionTests PRIVATE -Wl,--gc-sections)
    endif()
endif()
add_test(NAME StackImage.DecodeTransaction COMMAND StackImagePayloadDecodeTransactionTests)
set_tests_properties(StackImage.DecodeTransaction PROPERTIES LABELS "image;persistence;cpu" TIMEOUT 30)
