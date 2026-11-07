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
    Raw::RawGpuMemoryBudgetInput result;
    for (const auto& adapter : adapters) {
        if (!adapter.memory.queryAvailable) continue;
        if (renderingAdapter && adapter.luid == *renderingAdapter) return adapter.memory;
        // Without a reliable match, never assume the largest GPU is rendering.
        // Use the least measured working headroom, including its reserve policy.
        if (!result.queryAvailable ||
            Raw::ResolveRawGpuMemoryBudget(adapter.memory).workingBudgetBytes <
            Raw::ResolveRawGpuMemoryBudget(result).workingBudgetBytes)
            result = adapter.memory;
    }
    return result;
}
} // namespace Stack::Renderer
