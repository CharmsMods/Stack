# Bounded CPU checks of the common production motion implementation. The
# Windows prepared-sample dependency closure below uses real production code.
add_library(StackAlignmentCheckMath OBJECT EXCLUDE_FROM_ALL
    src/Raw/MultiFrameDenoise/Contracts.cpp
    src/Raw/MultiFrameDenoise/GlobalRegistration.cpp
    src/Raw/MultiFrameDenoise/LocalMotion.cpp
    src/Raw/MultiFrameDenoise/LocalMotionPatch.cpp
    src/Raw/MultiFrameDenoise/PyramidVariance.cpp
    src/Raw/MultiFrameDenoise/SameCfaSampler.cpp)
target_include_directories(StackAlignmentCheckMath PRIVATE src src/ThirdParty)

add_executable(StackBurstAlignmentTests EXCLUDE_FROM_ALL
    tools/tests/burst_alignment_tests.cpp
    src/App/Validation/Suites/MfdPhase5Validation.cpp
    $<TARGET_OBJECTS:StackAlignmentCheckMath>)
add_executable(StackAlignmentProposalTests EXCLUDE_FROM_ALL
    tools/tests/alignment_proposal_tests.cpp
    src/Raw/Bracketing/AlignmentProposalInput.cpp
    src/Raw/Bracketing/CaptureMotion.cpp
    src/NodeMath/ContractTypes.cpp
    $<TARGET_OBJECTS:StackAlignmentCheckMath>)
add_executable(StackPyramidVarianceTests EXCLUDE_FROM_ALL
    tools/tests/pyramid_variance_tests.cpp
    src/Raw/MultiFrameDenoise/PyramidVariance.cpp)
if(WIN32)
    # SameCfaSampler also owns prepared-sample entry points. Provide their
    # complete CPU dependencies rather than relying on /OPT:REF to discard
    # unresolved references. Preparation uses the Windows scratch-budget API.
    target_sources(StackAlignmentCheckMath PRIVATE
        src/Raw/MultiFrameDenoise/Preparation.cpp
        src/Raw/MultiFrameDenoise/NoiseModel.cpp
        src/Raw/RawProcessingMath.cpp
        src/Raw/RawTechnicalEvidence.cpp
        src/Raw/RawImageData.cpp)
    # The existing image-derived anchor fixture uses the real Windows tile
    # cache and scratch-budget owner; other platforms still check transport.
    target_sources(StackAlignmentProposalTests PRIVATE
        src/Raw/Bracketing/PreparedTileSampler.cpp
        src/Raw/Bracketing/PartialCfaReconstruction.cpp)
endif()

foreach(target StackAlignmentCheckMath StackBurstAlignmentTests
    StackAlignmentProposalTests StackPyramidVarianceTests)
    target_include_directories(${target} PRIVATE src src/ThirdParty)
    target_link_libraries(${target} PRIVATE imgui_core)
    if(MSVC)
        target_compile_options(${target} PRIVATE /Gy /Gw /utf-8)
        if(NOT target STREQUAL "StackAlignmentCheckMath")
            target_link_options(${target} PRIVATE /OPT:REF)
        endif()
    elseif(APPLE)
        target_compile_options(${target} PRIVATE -ffunction-sections -fdata-sections)
        if(NOT target STREQUAL "StackAlignmentCheckMath")
            target_link_options(${target} PRIVATE -Wl,-dead_strip)
        endif()
    else()
        target_compile_options(${target} PRIVATE -ffunction-sections -fdata-sections)
        if(NOT target STREQUAL "StackAlignmentCheckMath")
            target_link_options(${target} PRIVATE -Wl,--gc-sections)
        endif()
    endif()
endforeach()

add_test(NAME StackBurst.Alignment COMMAND StackBurstAlignmentTests)
add_test(NAME StackBurst.AlignmentProposals COMMAND StackAlignmentProposalTests)
add_test(NAME StackBurst.PyramidVariance COMMAND StackPyramidVarianceTests)
set_tests_properties(StackBurst.Alignment StackBurst.AlignmentProposals
    StackBurst.PyramidVariance PROPERTIES LABELS "burst;alignment;math;cpu")
