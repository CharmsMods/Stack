#pragma once

#include "Raw/RawGpuMemoryBudget.h"
#include <array>
#include <optional>
#include <vector>

namespace Stack::Renderer {
struct GpuAdapterBudget {
    std::array<unsigned char, 8> luid{};
    Raw::RawGpuMemoryBudgetInput memory;
};

inline Raw::RawGpuMemoryBudgetInput SelectGpuMemoryBudget(
    const std::vector<GpuAdapterBudget>& adapters,
    const std::optional<std::array<unsigned char, 8>>& renderingAdapter) {
    if (renderingAdapter) {
        for (const auto& adapter : adapters) {
            if (adapter.luid != *renderingAdapter) continue;
            if (adapter.memory.queryAvailable && adapter.memory.localBudgetBytes > 0)
                return adapter.memory;
            return {};
        }
        // An identified context cannot borrow an unrelated adapter's headroom.
        return {};
    }
    Raw::RawGpuMemoryBudgetInput result;
    for (const auto& adapter : adapters) {
        if (!adapter.memory.queryAvailable || adapter.memory.localBudgetBytes == 0) continue;
        // Without a rendering-device identity, never assume the largest GPU is rendering.
        // Use the least measured working headroom, including its reserve policy.
        if (!result.queryAvailable ||
            Raw::ResolveRawGpuMemoryBudget(adapter.memory).workingBudgetBytes <
            Raw::ResolveRawGpuMemoryBudget(result).workingBudgetBytes)
            result = adapter.memory;
    }
    return result;
}
} // namespace Stack::Renderer
