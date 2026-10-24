add_executable(StackLibraryProjectExportTests
    tools/tests/library_project_export_tests.cpp
    src/Library/Internal/LibraryProjectExport.cpp
    src/Persistence/ProjectStore.cpp
    src/Persistence/RawProjectModel.cpp
    src/Utils/AtomicExportFile.cpp
    src/Raw/RawTechnicalEvidence.cpp
    src/NodeMath/Uuid.cpp
    src/Persistence/RawProjectEditPipeline.cpp
    src/Persistence/MultiFrameGraph.cpp
    src/Raw/RawCaptureCompatibility.cpp
    src/Raw/MultiFrameDenoise/Contracts.cpp
    src/Raw/MultiFrameHdr/Contracts.cpp
    src/Raw/Bracketing/Recipe.cpp
    src/Raw/RawDevelopmentRecipe.cpp
    src/Raw/RawGraphOperation.cpp
    src/Raw/Tone/SceneTone.cpp
    src/Raw/RawZoneAreaRecipe.cpp
    src/Raw/RawColorCalibration.cpp
    src/Raw/Denoise/RawDenoiseControlMap.cpp
    src/Raw/Detail/DetailContrast.cpp
)
target_include_directories(StackLibraryProjectExportTests PRIVATE src)
target_link_libraries(StackLibraryProjectExportTests PRIVATE Threads::Threads)
if(MSVC)
    target_compile_options(StackLibraryProjectExportTests PRIVATE /Gy /Gw)
    target_link_options(StackLibraryProjectExportTests PRIVATE /OPT:REF /INCREMENTAL:NO)
else()
    target_compile_options(StackLibraryProjectExportTests PRIVATE -ffunction-sections -fdata-sections)
    if(APPLE)
        target_link_options(StackLibraryProjectExportTests PRIVATE -Wl,-dead_strip)
    else()
        target_link_options(StackLibraryProjectExportTests PRIVATE -Wl,--gc-sections)
    endif()
endif()
add_test(NAME StackLibrary.ProjectExport COMMAND StackLibraryProjectExportTests)
set_tests_properties(StackLibrary.ProjectExport PROPERTIES LABELS "persistence;export;cpu" TIMEOUT 30)
