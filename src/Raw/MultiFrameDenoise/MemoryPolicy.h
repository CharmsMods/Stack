#pragma once

#include <cstdint>
#include <string>

namespace Raw::Mfd {

inline constexpr std::uint64_t kMemoryPolicyGibibyte =
    1024ull * 1024ull * 1024ull;

struct PhysicalMemorySnapshot {
    std::uint64_t totalPhysicalBytes = 0;
    std::uint64_t availablePhysicalBytes = 0;
    bool valid = false;
};

struct MfdProcessingMemoryBudgetDecision {
    bool valid = false;
    bool automatic = true;
    bool constrainedToSafeCeiling = false;
    std::uint64_t requestedBytes = 0;
    std::uint64_t budgetBytes = 0;
    std::uint64_t reserveBytes = 0;
    std::uint64_t safeCeilingBytes = 0;
    PhysicalMemorySnapshot physicalMemory;
    std::string message;
};

PhysicalMemorySnapshot QueryPhysicalMemorySnapshot();

MfdProcessingMemoryBudgetDecision ResolveMfdProcessingMemoryBudget(
    double requestedBudgetGiB,
    const PhysicalMemorySnapshot& physicalMemory);

} // namespace Raw::Mfd
