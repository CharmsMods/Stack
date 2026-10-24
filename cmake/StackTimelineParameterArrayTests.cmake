add_executable(StackTimelineParameterArrayTests
    tools/tests/timeline_parameter_array_tests.cpp
    src/Editor/Timeline/TimelineAnimation.cpp
    src/Editor/Timeline/TimelineParameterEdits.cpp
    src/NodeMath/FirstClassValue.cpp
    src/NodeMath/ContractTypes.cpp)
target_include_directories(StackTimelineParameterArrayTests PRIVATE src "${imgui_SOURCE_DIR}")
if(MSVC)
    target_compile_options(StackTimelineParameterArrayTests PRIVATE /Gy /Gw)
    target_link_options(StackTimelineParameterArrayTests PRIVATE /OPT:REF /INCREMENTAL:NO)
else()
    target_compile_options(StackTimelineParameterArrayTests PRIVATE -ffunction-sections -fdata-sections)
    if(APPLE)
        target_link_options(StackTimelineParameterArrayTests PRIVATE -Wl,-dead_strip)
    else()
        target_link_options(StackTimelineParameterArrayTests PRIVATE -Wl,--gc-sections)
    endif()
endif()
add_test(NAME StackTimeline.ParameterArrayEdits COMMAND StackTimelineParameterArrayTests)
set_tests_properties(StackTimeline.ParameterArrayEdits PROPERTIES LABELS "timeline;history;cpu" TIMEOUT 30)
