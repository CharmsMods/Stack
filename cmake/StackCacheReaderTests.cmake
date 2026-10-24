# Link actual cache-reader/writer functions without pulling unrelated GPU
# processors into this small, headless file-format regression executable.
add_executable(StackCacheReaderTests
    tools/tests/cache_reader_tests.cpp
    src/Editor/MultiFrameResultCache.cpp
    src/Editor/NodeGraph/Serialization/EditorNodeGraphRawSerialization.cpp
    src/Raw/RawMetadataSerialization.cpp
    src/Raw/RawTechnicalEvidence.cpp
    src/Raw/RawImageData.cpp
    src/Raw/MultiFrameHdr/Processor.cpp
    src/NodeMath/ContractTypes.cpp)
target_include_directories(StackCacheReaderTests PRIVATE src)
target_link_libraries(StackCacheReaderTests PRIVATE Threads::Threads)
if(MSVC)
    target_compile_options(StackCacheReaderTests PRIVATE /Gy /Gw)
    target_link_options(StackCacheReaderTests PRIVATE /OPT:REF /INCREMENTAL:NO)
else()
    target_compile_options(StackCacheReaderTests PRIVATE -ffunction-sections -fdata-sections)
    target_link_options(StackCacheReaderTests PRIVATE -Wl,--gc-sections)
endif()
add_test(NAME StackPersistence.CacheReaders COMMAND StackCacheReaderTests)
set_tests_properties(StackPersistence.CacheReaders PROPERTIES LABELS "persistence;cache;cpu" TIMEOUT 30)
