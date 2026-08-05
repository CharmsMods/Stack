#pragma once

#include <cstdint>

namespace Stack::EditorRenderScheduling {

inline bool AcceptSubmission(
    std::uint64_t generation,
    bool stopRequested,
    std::uint64_t invalidBeforeGeneration,
    std::uint64_t latestSubmittedGeneration) {
    return !stopRequested &&
        generation >= invalidBeforeGeneration &&
        generation >= latestSubmittedGeneration;
}

inline bool DiscardCompletedResult(
    std::uint64_t generation,
    bool stopRequested,
    std::uint64_t invalidBeforeGeneration,
    std::uint64_t latestSubmittedGeneration,
    bool carriesCancellationAcknowledgement) {
    if (stopRequested) {
        return true;
    }
    const bool stale =
        generation < invalidBeforeGeneration ||
        generation < latestSubmittedGeneration;
    return stale && !carriesCancellationAcknowledgement;
}

} // namespace Stack::EditorRenderScheduling
