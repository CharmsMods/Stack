#pragma once
#include <cstddef>
#include <cstdint>

namespace Stack::Renderer::GraphExecution {
// Retention is optional, including on allocation-heavy small-preview paths.
// Bound both bytes and object count before admitting another retired target.
inline bool CanRetainGraphTransientTarget(std::uint64_t bytes, std::uint64_t resident,
    std::uint64_t budget, std::size_t entries, std::size_t maximumEntries) {
    return bytes > 0 && entries < maximumEntries && resident <= budget && bytes <= budget-resident;
}
} // namespace Stack::Renderer::GraphExecution
