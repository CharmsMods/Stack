add_executable(StackArchiveExportTests
    tools/tests/archive_export_tests.cpp
    src/Persistence/StackBinaryFormat.cpp
    src/Utils/AtomicExportFile.cpp
    src/ThirdParty/stb_image_impl.cpp)
target_include_directories(StackArchiveExportTests PRIVATE src)
target_link_libraries(StackArchiveExportTests PRIVATE Threads::Threads)
if(MSVC)
    target_compile_options(StackArchiveExportTests PRIVATE /Gy /Gw)
    target_link_options(StackArchiveExportTests PRIVATE /OPT:REF /INCREMENTAL:NO)
else()
    target_compile_options(StackArchiveExportTests PRIVATE -ffunction-sections -fdata-sections)
    if(APPLE)
        target_link_options(StackArchiveExportTests PRIVATE -Wl,-dead_strip)
    else()
        target_link_options(StackArchiveExportTests PRIVATE -Wl,--gc-sections)
    endif()
endif()
add_test(NAME StackPersistence.ArchiveExport COMMAND StackArchiveExportTests)
set_tests_properties(StackPersistence.ArchiveExport PROPERTIES LABELS "persistence;export;cpu" TIMEOUT 30)
