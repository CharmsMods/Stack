# Actual native map input, UI helpers and control-map math, without GL rendering.
add_executable(StackDenoiseMapOwnerTests tools/tests/denoise_map_owner_tests.cpp
    src/Editor/Internal/RawLab/RawDenoiseMapEditor.cpp
    src/Editor/Internal/RawLab/RawLabUiSupport.cpp src/Raw/Denoise/RawDenoiseControlMap.cpp)
target_include_directories(StackDenoiseMapOwnerTests PRIVATE src)
target_link_libraries(StackDenoiseMapOwnerTests PRIVATE imgui_core)
if(MSVC)
    target_compile_options(StackDenoiseMapOwnerTests PRIVATE /Gy /Gw)
    target_link_options(StackDenoiseMapOwnerTests PRIVATE /OPT:REF /INCREMENTAL:NO)
else()
    target_compile_options(StackDenoiseMapOwnerTests PRIVATE -ffunction-sections -fdata-sections)
    if(APPLE)
        target_link_options(StackDenoiseMapOwnerTests PRIVATE -Wl,-dead_strip)
    else()
        target_link_options(StackDenoiseMapOwnerTests PRIVATE -Wl,--gc-sections)
    endif()
endif()
add_test(NAME StackRaw.DenoiseMapOwner COMMAND StackDenoiseMapOwnerTests)
set_tests_properties(StackRaw.DenoiseMapOwner PROPERTIES LABELS "raw;ui;input;cpu" TIMEOUT 30)
