add_executable(StackCreamPaletteTests tools/tests/cream_palette_tests.cpp
    src/App/settings/CreamPalette.cpp src/App/settings/CreamPalettePersistence.cpp)
target_include_directories(StackCreamPaletteTests PRIVATE src)
target_link_libraries(StackCreamPaletteTests PRIVATE imgui_core)
add_test(NAME StackApp.CreamPalette COMMAND StackCreamPaletteTests)
set_tests_properties(StackApp.CreamPalette PROPERTIES LABELS "app;appearance;cpu")
