#pragma once

#include <atomic>
#include <cstdint>

namespace Stack::Project {

// Shared catalog invalidation only. Documents and save state remain owned by
// their sessions. Publish after a successful store write, including background
// bracket creation and Save As, so every browser can observe the change.
inline std::atomic<std::uint64_t>& ProjectCatalogChangeCounter() {
    static std::atomic<std::uint64_t> revision{0};
    return revision;
}

inline void NotifyProjectCatalogChanged() {
    ProjectCatalogChangeCounter().fetch_add(1, std::memory_order_release);
}

inline std::uint64_t ProjectCatalogRevision() {
    return ProjectCatalogChangeCounter().load(std::memory_order_acquire);
}

} // namespace Stack::Project
