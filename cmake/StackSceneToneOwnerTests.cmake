# Full native editor and real tone math. Section elimination keeps unrelated
# recipe processing services out of this bounded UI-only test executable.
add_executable(StackSceneToneOwnerTests tools/tests/scene_tone_owner_tests.cpp
    src/Raw/Tone/SceneTone.cpp src/Raw/RawDevelopmentRecipe.cpp)
target_include_directories(StackSceneToneOwnerTests PRIVATE src)
target_link_libraries(StackSceneToneOwnerTests PRIVATE imgui_core)
if(MSVC)
    target_compile_options(StackSceneToneOwnerTests PRIVATE /Gy /Gw)
    target_link_options(StackSceneToneOwnerTests PRIVATE /OPT:REF /INCREMENTAL:NO)
else()
    target_compile_options(StackSceneToneOwnerTests PRIVATE -ffunction-sections -fdata-sections)
    if(APPLE)
        target_link_options(StackSceneToneOwnerTests PRIVATE -Wl,-dead_strip)
    else()
        target_link_options(StackSceneToneOwnerTests PRIVATE -Wl,--gc-sections)
    endif()
endif()
add_test(NAME StackRaw.SceneToneOwner COMMAND StackSceneToneOwnerTests)
set_tests_properties(StackRaw.SceneToneOwner PROPERTIES LABELS "raw;ui;input;cpu" TIMEOUT 30)
