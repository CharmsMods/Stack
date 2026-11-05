add_executable(StackProjectCoverTransactionTests
    tools/tests/project_cover_transaction_tests.cpp
    src/Persistence/ClosetMerge.cpp
    src/Persistence/ProjectStore.cpp
    src/Persistence/RawProjectModel.cpp
    src/Raw/RawTechnicalEvidence.cpp
    src/Raw/RawImageData.cpp
    src/NodeMath/Uuid.cpp
    src/NodeMath/ToneCurveParameters.cpp
    src/Persistence/RawProjectEditPipeline.cpp
    src/Persistence/MultiFrameGraph.cpp
    src/Raw/RawCaptureCompatibility.cpp
    src/Raw/MultiFrameDenoise/Contracts.cpp
    src/Raw/MultiFrameHdr/Contracts.cpp
    src/Raw/Bracketing/Recipe.cpp
    src/Raw/RawDevelopmentRecipe.cpp src/Raw/ColorWarpMath.cpp src/Raw/RawColorWarpRecipe.cpp
    src/Raw/RawGraphOperation.cpp
    src/Raw/Tone/SceneTone.cpp
    src/Raw/RawZoneAreaRecipe.cpp
    src/Raw/RawZoneAreaGain.cpp
    src/Raw/RawColorCalibration.cpp
    src/Raw/Denoise/RawDenoiseControlMap.cpp
    src/Raw/Detail/DetailContrast.cpp
)
target_include_directories(StackProjectCoverTransactionTests PRIVATE src)
target_link_libraries(StackProjectCoverTransactionTests PRIVATE Threads::Threads)
if(MSVC)
    target_compile_options(StackProjectCoverTransactionTests PRIVATE /Gy /Gw)
    target_link_options(StackProjectCoverTransactionTests PRIVATE /OPT:REF /INCREMENTAL:NO)
else()
    target_compile_options(StackProjectCoverTransactionTests PRIVATE -ffunction-sections -fdata-sections)
    if(APPLE)
        target_link_options(StackProjectCoverTransactionTests PRIVATE -Wl,-dead_strip)
    else()
        target_link_options(StackProjectCoverTransactionTests PRIVATE -Wl,--gc-sections)
    endif()
endif()
add_test(NAME StackProject.CoverTransaction COMMAND StackProjectCoverTransactionTests)
set_tests_properties(StackProject.CoverTransaction PROPERTIES LABELS "persistence;cover;cpu" TIMEOUT 30)


add_executable(StackBracketMasterTests tools/tests/bracket_master_tests.cpp
    src/Raw/Internal/DngTiffReader.cpp src/Persistence/BracketDngMaster.cpp src/BurstEngine/OutputDng.cpp src/Raw/RawColorTransform.cpp)
target_include_directories(StackBracketMasterTests PRIVATE src "${CMAKE_BINARY_DIR}/generated/src")
target_link_libraries(StackBracketMasterTests PRIVATE StackBracketExr)
if(MSVC)
    target_compile_options(StackBracketMasterTests PRIVATE /Gy /Gw)
    target_link_options(StackBracketMasterTests PRIVATE /OPT:REF /INCREMENTAL:NO)
endif()
add_test(NAME StackProject.BracketMasters COMMAND StackBracketMasterTests)
