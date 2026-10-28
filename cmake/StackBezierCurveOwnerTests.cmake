# Full response wrapper/shared interaction and actual curve math/style. Glass
# presentation doubles avoid a GL dependency while native input stays intact.
add_executable(StackBezierCurveOwnerTests tools/tests/bezier_curve_owner_tests.cpp
    src/Editor/Internal/RawLab/RawLabCurveEditor.cpp src/Editor/GraphUI/GraphPlotStyle.cpp
    src/Raw/Tone/SceneTone.cpp src/Raw/RawDevelopmentRecipe.cpp)
target_include_directories(StackBezierCurveOwnerTests PRIVATE src)
target_link_libraries(StackBezierCurveOwnerTests PRIVATE imgui_core)
if(MSVC)
    target_compile_options(StackBezierCurveOwnerTests PRIVATE /Gy /Gw)
    target_link_options(StackBezierCurveOwnerTests PRIVATE /OPT:REF /INCREMENTAL:NO)
else()
    target_compile_options(StackBezierCurveOwnerTests PRIVATE -ffunction-sections -fdata-sections)
    if(APPLE)
        target_link_options(StackBezierCurveOwnerTests PRIVATE -Wl,-dead_strip)
    else()
        target_link_options(StackBezierCurveOwnerTests PRIVATE -Wl,--gc-sections)
    endif()
endif()
add_test(NAME StackRaw.BezierCurveOwner COMMAND StackBezierCurveOwnerTests)
set_tests_properties(StackRaw.BezierCurveOwner PROPERTIES LABELS "raw;ui;input;cpu" TIMEOUT 30)
