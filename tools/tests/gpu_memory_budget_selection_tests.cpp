#include "Renderer/GpuMemoryBudgetSelection.h"
#include "Editor/GraphRenderMemory.h"
#include "Editor/RawRenderPlanning.h"
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
    require(!SelectGpuMemoryBudget(adapters, std::array<unsigned char,8>{3}).queryAvailable,
        "A known unmatched rendering adapter borrowed another adapter's budget");
    auto unavailable = integrated; unavailable.memory.queryAvailable = false;
    require(!SelectGpuMemoryBudget({discrete, unavailable}, unavailable.luid).queryAvailable,
        "A failed matched query borrowed another adapter's budget");
    require(SelectGpuMemoryBudget({unavailable, discrete}, std::nullopt).localBudgetBytes == discrete.memory.localBudgetBytes,
        "An unidentified adapter used unavailable telemetry");
    auto zeroBudget = integrated; zeroBudget.memory.localBudgetBytes = 0;
    require(!SelectGpuMemoryBudget({discrete, zeroBudget}, zeroBudget.luid).queryAvailable,
        "A matched zero budget was reported as measured headroom");
    require(SelectGpuMemoryBudget({zeroBudget, discrete}, std::nullopt).localBudgetBytes == discrete.memory.localBudgetBytes,
        "An unidentified adapter treated a zero budget as measured headroom");
    auto pressured = discrete; pressured.memory.localUsageBytes = 8000 * mib;
    require(SelectGpuMemoryBudget({integrated, pressured}, std::nullopt).localUsageBytes == pressured.memory.localUsageBytes,
        "Fallback compared capacity instead of available working headroom");
    require(!SelectGpuMemoryBudget({}, std::nullopt).queryAvailable, "Missing devices fabricated a budget");
    const auto fallback = Raw::ResolveRawGpuMemoryBudget({});
    require(fallback.usedFallback && fallback.workingBudgetBytes == 512 * mib,
        "Unmeasured memory must have a finite working allowance");
    require(Raw::ResolveRawGpuMemoryBudget({0, 0, true}).usedFallback,
        "A zero DXGI budget must remain unmeasured");
    require(!Raw::ResolveRawFullFramePreviewDecision(7952, 5304, 16384,
        fallback.workingBudgetBytes, false, 16).allowed,
        "A missing query must not admit a whole 42 MP float32 working set");
    require(Raw::ResolveRawFullFramePreviewDecision(1024, 683, 16384,
        fallback.workingBudgetBytes, false, 16).allowed,
        "A bounded editing preview must still fit the unmeasured allowance");
    const auto large = Raw::ResolveRawGpuMemoryBudget({12 * 1024 * mib, 2 * 1024 * mib, true});
    require(!large.usedFallback && large.workingBudgetBytes > 3 * 1024 * mib,
        "Measured strong GPUs must not inherit the fallback cap");
    const auto exhausted = Raw::ResolveRawGpuMemoryBudget({1024 * mib, 1024 * mib, true});
    require(!exhausted.usedFallback && exhausted.workingBudgetBytes == 0,
        "Exhausted measured memory must not be replaced by a fallback allowance");
    using namespace Stack::GraphRendering;
    require(CacheAllowance(false, fallback.workingBudgetBytes, UINT64_MAX, 128 * mib) == 64 * mib,
        "Unknown-memory caches must stay bounded without adding resident memory back");
    require(CacheAllowance(false, fallback.workingBudgetBytes, 0, 500 * mib) == 12 * mib,
        "Unknown-memory caches must leave room for admitted active work");
    require(CacheAllowance(false, fallback.workingBudgetBytes, 0, UINT64_MAX) == 0,
        "Oversized work must not receive speculative cache memory");
    require(CacheAllowance(true, 100, 50, 120) == 30,
        "Measured admission should credit reclaimable owned cache bytes");
    require(ViewportSectionUploadBytes(1024, 1024, false) == 17 * mib &&
        ViewportSectionUploadBytes(1024, 1024, true) == 34 * mib,
        "Section reload admission must include native pixels and paired display copies");
    require(ViewportSectionUploadBytes(1025, 1025, true) ==
        (1025ull * 1025 + 257ull * 257) * 32,
        "Partial-section display dimensions must round upward");
    Stack::EditorRendering::RawRenderPlanInput exportInput;
    exportInput.exportRequested = true;
    exportInput.fullFrameWidth = 7952; exportInput.fullFrameHeight = 5304;
    exportInput.maximumTextureSize = 16384; exportInput.interactiveTargetEdge = 1024;
    exportInput.workingBudgetBytes = fallback.workingBudgetBytes;
    const auto exportPlan = Stack::EditorRendering::BuildRawRenderPlan(exportInput);
    require(exportPlan.targetEdge == 0 && exportPlan.purpose == RawRenderPurpose::ExplicitExport,
        "Memory fallback must never silently lower explicit export resolution");
    std::cout << "GPU budget selection, bounded fallback, section admission and export policy passed\n";
} catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
