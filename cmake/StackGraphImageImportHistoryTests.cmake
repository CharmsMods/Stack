add_executable(StackGraphImageImportHistoryTests
    tools/tests/project_graph_image_import_history_tests.cpp
    src/Editor/NodeGraph/EditorNodeGraph.cpp
    src/Editor/NodeGraph/Model/EditorNodeGraphMutation.cpp
    src/Async/TaskSystem.cpp
    src/Project/GraphImagePayload.cpp
    src/NodeMath/FirstClassValue.cpp
    src/NodeMath/DescriptorSerialization.cpp
    src/NodeMath/ContractTypes.cpp)
target_include_directories(StackGraphImageImportHistoryTests PRIVATE src)
find_package(Threads REQUIRED)
target_link_libraries(StackGraphImageImportHistoryTests PRIVATE Threads::Threads)
# This CPU test uses graph membership/history, not the rendering/node registry.
# Discard unrelated graph entry points from the shared implementation units.
if(MSVC)
    target_compile_options(StackGraphImageImportHistoryTests PRIVATE /Gy /Gw)
    target_link_options(StackGraphImageImportHistoryTests PRIVATE /OPT:REF)
else()
    target_compile_options(StackGraphImageImportHistoryTests PRIVATE -ffunction-sections -fdata-sections)
    if(APPLE)
        target_link_options(StackGraphImageImportHistoryTests PRIVATE -Wl,-dead_strip)
    else()
        target_link_options(StackGraphImageImportHistoryTests PRIVATE -Wl,--gc-sections)
    endif()
endif()
add_test(NAME StackProject.GraphImageImportHistory COMMAND StackGraphImageImportHistoryTests)
set_tests_properties(StackProject.GraphImageImportHistory PROPERTIES LABELS "project;history;async;cpu" TIMEOUT 30)
