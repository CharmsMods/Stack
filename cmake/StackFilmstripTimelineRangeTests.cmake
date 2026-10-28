set(filmstrip_timeline_range_test "${CMAKE_CURRENT_BINARY_DIR}/generated/filmstrip_timeline_range_tests.cpp")
add_custom_command(OUTPUT "${filmstrip_timeline_range_test}"
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tools/tests/filmstrip_timeline_range_tests.py"
        --output "${filmstrip_timeline_range_test}"
    DEPENDS tools/tests/filmstrip_timeline_range_tests.py
        tools/tests/filmstrip_timeline_range_tests.cpp
        src/Editor/Internal/RawLab/EditorModuleRawLabFilmstripTimeline.cpp
    VERBATIM)
add_executable(StackFilmstripTimelineRangeTests "${filmstrip_timeline_range_test}")
add_test(NAME StackRaw.FilmstripTimelineRange COMMAND StackFilmstripTimelineRangeTests)
set_tests_properties(StackRaw.FilmstripTimelineRange PROPERTIES LABELS "raw;gallery;ui;cpu" TIMEOUT 30)
