#include "Raw/MultiFrameDenoise/MemoryPolicy.h"

#include <algorithm>
#include <cmath>
#include <limits>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#elif defined(__unix__) || defined(__APPLE__)
#include <unistd.h>
#endif

namespace Raw::Mfd {
namespace {

constexpr std::uint64_t kMinimumOperatingReserveBytes =
    2ull * kMemoryPolicyGibibyte;
constexpr std::uint64_t kMinimumUsableBudgetBytes =
    512ull * 1024ull * 1024ull;
constexpr double kTotalMemoryReserveFraction = 0.125;
constexpr double kMaximumManualBudgetGiB = 256.0;

std::uint64_t FractionBytes(
    std::uint64_t bytes,
    double fraction) {
    const long double value =
        static_cast<long double>(bytes) * fraction;
    if (value >= static_cast<long double>(
            std::numeric_limits<std::uint64_t>::max())) {
        return std::numeric_limits<std::uint64_t>::max();
    }
    return static_cast<std::uint64_t>(value);
}

} // namespace

PhysicalMemorySnapshot QueryPhysicalMemorySnapshot() {
    PhysicalMemorySnapshot snapshot;
#if defined(_WIN32)
    MEMORYSTATUSEX status {};
    status.dwLength = sizeof(status);
    if (GlobalMemoryStatusEx(&status) != FALSE) {
        snapshot.totalPhysicalBytes = status.ullTotalPhys;
        snapshot.availablePhysicalBytes = status.ullAvailPhys;
        snapshot.availableCommitBytes = status.ullAvailPageFile;
        snapshot.commitLimitKnown = true;
        snapshot.valid = snapshot.totalPhysicalBytes > 0u &&
            snapshot.availablePhysicalBytes > 0u &&
            snapshot.availablePhysicalBytes <= snapshot.totalPhysicalBytes;
    }
#elif defined(__unix__) || defined(__APPLE__)
    const long pageSize = sysconf(_SC_PAGESIZE);
    const long totalPages = sysconf(_SC_PHYS_PAGES);
    const long availablePages = sysconf(_SC_AVPHYS_PAGES);
    if (pageSize > 0 && totalPages > 0 && availablePages > 0) {
        snapshot.totalPhysicalBytes =
            static_cast<std::uint64_t>(pageSize) *
            static_cast<std::uint64_t>(totalPages);
        snapshot.availablePhysicalBytes =
            static_cast<std::uint64_t>(pageSize) *
            static_cast<std::uint64_t>(availablePages);
        snapshot.valid = snapshot.availablePhysicalBytes <=
            snapshot.totalPhysicalBytes;
    }
#endif
    return snapshot;
}

MfdProcessingMemoryBudgetDecision ResolveMfdProcessingMemoryBudget(
    double requestedBudgetGiB,
    const PhysicalMemorySnapshot& physicalMemory) {
    MfdProcessingMemoryBudgetDecision decision;
    decision.physicalMemory = physicalMemory;
    decision.automatic = requestedBudgetGiB == 0.0;
    if (!std::isfinite(requestedBudgetGiB) ||
        requestedBudgetGiB < 0.0 ||
        requestedBudgetGiB > kMaximumManualBudgetGiB) {
        decision.message =
            "MFD memory budget must be 0 (automatic) or between 0 and 256 GiB.";
        return decision;
    }

    if (physicalMemory.valid) {
        decision.reserveBytes = std::max(
            kMinimumOperatingReserveBytes,
            FractionBytes(
                physicalMemory.totalPhysicalBytes,
                kTotalMemoryReserveFraction));
        decision.safeCeilingBytes =
            physicalMemory.availablePhysicalBytes > decision.reserveBytes
            ? physicalMemory.availablePhysicalBytes - decision.reserveBytes
            : physicalMemory.availablePhysicalBytes / 2u;
    } else {
        // A conservative fallback keeps processing usable on platforms where
        // physical-memory telemetry is unavailable. Manual mode remains exact.
        decision.reserveBytes = kMinimumOperatingReserveBytes;
        decision.safeCeilingBytes =
            4ull * kMemoryPolicyGibibyte;
    }

    if (decision.automatic) {
        decision.requestedBytes = decision.safeCeilingBytes;
    } else {
        const long double requestedBytes =
            static_cast<long double>(requestedBudgetGiB) *
            static_cast<long double>(kMemoryPolicyGibibyte);
        decision.requestedBytes = static_cast<std::uint64_t>(
            std::min(
                requestedBytes,
                static_cast<long double>(
                    std::numeric_limits<std::uint64_t>::max())));
    }
    decision.budgetBytes = physicalMemory.valid
        ? std::min(decision.requestedBytes, decision.safeCeilingBytes)
        : decision.requestedBytes;
    decision.constrainedToSafeCeiling =
        !decision.automatic &&
        decision.budgetBytes < decision.requestedBytes;
    if (decision.automatic && decision.budgetBytes == 0u) {
        // Automatic mode is advisory for interactive processing. Preserve a
        // non-zero planning value even under extreme pressure so the caller
        // can save first and attempt staged allocations instead of refusing.
        decision.budgetBytes = 1u;
    }
    if (!decision.automatic &&
        decision.budgetBytes < kMinimumUsableBudgetBytes) {
        decision.message =
            "Less than 512 MiB is safely available for MFD after the operating reserve.";
        return decision;
    }

    decision.valid = true;
    if (decision.automatic) {
        decision.message =
            "Automatic MultiFrame memory is an advisory target after the operating reserve; interactive processing may attempt more.";
    } else if (decision.constrainedToSafeCeiling) {
        decision.message =
            "The manual MFD budget was bounded by currently available physical memory and the protected operating reserve.";
    } else {
        decision.message = "Manual MFD memory budget accepted.";
    }
    return decision;
}

} // namespace Raw::Mfd
