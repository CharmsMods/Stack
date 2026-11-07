add_executable(StackGpuMemoryBudgetSelectionTests
    tools/tests/gpu_memory_budget_selection_tests.cpp
    src/Raw/RawGpuMemoryBudget.cpp)
target_include_directories(StackGpuMemoryBudgetSelectionTests PRIVATE src)
add_test(NAME StackRenderer.GpuMemoryBudgetSelection COMMAND StackGpuMemoryBudgetSelectionTests)
set_tests_properties(StackRenderer.GpuMemoryBudgetSelection PROPERTIES LABELS "renderer;memory;cpu" TIMEOUT 30)
