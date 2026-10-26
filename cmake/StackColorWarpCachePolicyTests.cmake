add_executable(StackColorWarpCachePolicyTests
    tools/tests/color_warp_cache_policy_tests.cpp
    src/Raw/RawDevelopmentRecipe.cpp
    src/Raw/RawZoneAreaRecipe.cpp
    src/Raw/RawZoneAreaGain.cpp
    src/Raw/RawColorCalibration.cpp
    src/Raw/Detail/DetailContrast.cpp
    src/Raw/Denoise/RawDenoiseControlMap.cpp)
target_include_directories(StackColorWarpCachePolicyTests PRIVATE src)
# Keep this CPU policy check independent of unrelated processing functions in
# the existing recipe translation units.
if(MSVC)
    target_compile_options(StackColorWarpCachePolicyTests PRIVATE /Gy /Gw)
    target_link_options(StackColorWarpCachePolicyTests PRIVATE /OPT:REF)
elseif(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang" AND NOT APPLE)
    target_compile_options(StackColorWarpCachePolicyTests PRIVATE -ffunction-sections -fdata-sections)
    target_link_options(StackColorWarpCachePolicyTests PRIVATE -Wl,--gc-sections)
endif()
add_test(NAME StackRenderer.ColorWarpCachePolicy COMMAND StackColorWarpCachePolicyTests)
set_tests_properties(StackRenderer.ColorWarpCachePolicy PROPERTIES
    LABELS "renderer;color;cache;cpu" TIMEOUT 30)
