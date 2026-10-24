set(source_image_history_test "${CMAKE_CURRENT_BINARY_DIR}/source_image_history_callback_tests.cpp")
add_custom_command(OUTPUT "${source_image_history_test}"
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tools/tests/source_image_history_callback_tests.py"
        --output "${source_image_history_test}"
    DEPENDS tools/tests/source_image_history_callback_tests.py
        src/Editor/Internal/EditorModulePersistence.cpp
        src/Editor/Internal/EditorModuleGraphHistory.cpp
        src/Editor/Internal/Project/EditorModuleProjectInteraction.cpp
    VERBATIM)
add_executable(StackSourceImageHistoryCallbackTests "${source_image_history_test}"
    src/Editor/NodeGraph/EditorNodeGraph.cpp
    src/Project/GraphImagePayload.cpp
    src/NodeMath/FirstClassValue.cpp
    src/NodeMath/DescriptorSerialization.cpp
    src/NodeMath/ContractTypes.cpp
    src/Editor/NodeGraph/EditorNodeGraphCompound.cpp
    src/Async/TaskSystem.cpp)
target_include_directories(StackSourceImageHistoryCallbackTests PRIVATE src)
target_link_libraries(StackSourceImageHistoryCallbackTests PRIVATE Threads::Threads)
if(MSVC)
    target_compile_options(StackSourceImageHistoryCallbackTests PRIVATE /Gy /Gw)
    target_link_options(StackSourceImageHistoryCallbackTests PRIVATE /OPT:REF /INCREMENTAL:NO)
else()
    target_compile_options(StackSourceImageHistoryCallbackTests PRIVATE -ffunction-sections -fdata-sections)
    if(APPLE)
        target_link_options(StackSourceImageHistoryCallbackTests PRIVATE -Wl,-dead_strip)
    else()
        target_link_options(StackSourceImageHistoryCallbackTests PRIVATE -Wl,--gc-sections)
    endif()
endif()
add_test(NAME StackProject.SourceImageHistoryCallback COMMAND StackSourceImageHistoryCallbackTests)
set_tests_properties(StackProject.SourceImageHistoryCallback PROPERTIES LABELS "project;history;async;cpu" TIMEOUT 30)
