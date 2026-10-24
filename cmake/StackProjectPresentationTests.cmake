add_executable(StackProjectPresentationTests
    tools/tests/project_presentation_persistence_tests.cpp
    src/Editor/Timeline/TimelinePersistence.cpp
    src/Editor/Timeline/TimelineFrameProducer.cpp
    src/Editor/Timeline/TimelinePlayback.cpp
    src/Editor/Timeline/TimelineAnimation.cpp)
target_include_directories(StackProjectPresentationTests PRIVATE src)
target_link_libraries(StackProjectPresentationTests PRIVATE imgui_core)
# Exercise the production readers without unrelated UI/registry entry points.
if(MSVC)
    target_compile_options(StackProjectPresentationTests PRIVATE /Gy /Gw)
    target_link_options(StackProjectPresentationTests PRIVATE /OPT:REF /INCREMENTAL:NO)
else()
    target_compile_options(StackProjectPresentationTests PRIVATE -ffunction-sections -fdata-sections)
    if(APPLE)
        target_link_options(StackProjectPresentationTests PRIVATE -Wl,-dead_strip)
    else()
        target_link_options(StackProjectPresentationTests PRIVATE -Wl,--gc-sections)
    endif()
endif()
add_test(NAME StackPersistence.ProjectPresentation COMMAND StackProjectPresentationTests)
set_tests_properties(StackPersistence.ProjectPresentation PROPERTIES LABELS "persistence;project;cpu" TIMEOUT 30)
