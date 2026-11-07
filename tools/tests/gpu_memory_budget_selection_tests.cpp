#include "Renderer/GpuMemoryBudgetSelection.h"
#include <iostream>
#include <stdexcept>

int main() try {
    using namespace Stack::Renderer;
    constexpr std::uint64_t mib = 1024ull * 1024;
    const GpuAdapterBudget integrated{{1}, {2048 * mib, 1024 * mib, true}};
    const GpuAdapterBudget discrete{{2}, {8192 * mib, 512 * mib, true}};
    const std::vector<GpuAdapterBudget> adapters{discrete, integrated};
    const auto require = [](bool ok, const char* message) { if (!ok) throw std::runtime_error(message); };
    require(SelectGpuMemoryBudget(adapters, integrated.luid).localBudgetBytes == integrated.memory.localBudgetBytes,
        "Integrated GL device used the discrete adapter budget");
    require(SelectGpuMemoryBudget(adapters, discrete.luid).localBudgetBytes == discrete.memory.localBudgetBytes,
        "Discrete GL device did not use its matched budget");
    require(SelectGpuMemoryBudget(adapters, std::nullopt).localBudgetBytes == integrated.memory.localBudgetBytes,
        "Unidentified adapter did not use conservative headroom");
    require(SelectGpuMemoryBudget(adapters, std::array<unsigned char,8>{3}).localBudgetBytes == integrated.memory.localBudgetBytes,
        "Unmatched adapter guessed the largest GPU");
    auto pressured = discrete; pressured.memory.localUsageBytes = 8000 * mib;
    require(SelectGpuMemoryBudget({integrated, pressured}, std::nullopt).localUsageBytes == pressured.memory.localUsageBytes,
        "Fallback compared capacity instead of available working headroom");
    require(!SelectGpuMemoryBudget({}, std::nullopt).queryAvailable, "Missing devices fabricated a budget");
    std::cout << "GPU budget selection: matched adapters and conservative fallback passed\n";
} catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
