#pragma once

#include <cstdint>
#include <string_view>

namespace Raw::MultiFrame {

// A completed worker may be adopted only while it still belongs to the same
// processing generation, project, input revision, and source set. The worker's
// success/cancel/failure state remains a separate processor-specific decision.
struct PublicationAttemptStamp {
    std::uint64_t generation = 0;
    std::string_view projectId;
    std::uint64_t inputRevision = 0;
};

struct ActivePublicationContext {
    std::uint64_t generation = 0;
    bool hasProject = false;
    std::string_view projectId;
    std::uint64_t inputRevision = 0;
    bool hasSourceSet = false;
};

enum class PublicationFenceResult : std::uint8_t {
    Current = 0,
    SupersededGeneration,
    NoActiveProject,
    DifferentProject,
    DifferentInputRevision,
    MissingSourceSet
};

inline PublicationFenceResult EvaluatePublicationFence(
    const PublicationAttemptStamp& attempt,
    const ActivePublicationContext& active) noexcept {
    if (attempt.generation != active.generation)
        return PublicationFenceResult::SupersededGeneration;
    if (!active.hasProject)
        return PublicationFenceResult::NoActiveProject;
    if (attempt.projectId != active.projectId)
        return PublicationFenceResult::DifferentProject;
    if (attempt.inputRevision != active.inputRevision)
        return PublicationFenceResult::DifferentInputRevision;
    if (!active.hasSourceSet)
        return PublicationFenceResult::MissingSourceSet;
    return PublicationFenceResult::Current;
}

} // namespace Raw::MultiFrame
