#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <string>
#include <vector>

namespace Stack::RawLocalRangeTargetInteraction {

inline constexpr float kDragDeadZonePixels = 8.0f;
inline constexpr float kDragPixelsPerEv = 120.0f;
inline constexpr float kCandidateMinimumWeight = 0.10f;
inline constexpr float kActiveZoneHysteresis = 0.05f;

enum class State {
    Hover,
    Armed,
    Refining,
    Creating
};

struct Candidate {
    std::string id;
    int zoneIndex = -1;
    float effectiveWeight = 0.0f;
    bool active = false;
};

inline float DragOffsetEv(float startMouseY, float currentMouseY) {
    const float signedPixels = startMouseY - currentMouseY;
    const float magnitude = std::abs(signedPixels);
    if (!std::isfinite(magnitude) || magnitude <= kDragDeadZonePixels) {
        return 0.0f;
    }
    return std::copysign(
        (magnitude - kDragDeadZonePixels) / kDragPixelsPerEv,
        signedPixels);
}

inline bool HasCrossedDragDeadZone(float startMouseY, float currentMouseY) {
    return std::abs(startMouseY - currentMouseY) > kDragDeadZonePixels;
}

inline int ChooseCandidate(const std::vector<Candidate>& candidates) {
    int strongest = -1;
    float strongestWeight = kCandidateMinimumWeight;
    int active = -1;
    float activeWeight = 0.0f;
    for (std::size_t index = 0; index < candidates.size(); ++index) {
        const Candidate& candidate = candidates[index];
        if (candidate.zoneIndex < 0 ||
            !std::isfinite(candidate.effectiveWeight) ||
            candidate.effectiveWeight < kCandidateMinimumWeight) {
            continue;
        }
        if (candidate.effectiveWeight > strongestWeight) {
            strongestWeight = candidate.effectiveWeight;
            strongest = static_cast<int>(index);
        }
        if (candidate.active && candidate.effectiveWeight > activeWeight) {
            activeWeight = candidate.effectiveWeight;
            active = static_cast<int>(index);
        }
    }
    if (active >= 0 &&
        (strongest < 0 ||
            activeWeight + kActiveZoneHysteresis >= strongestWeight)) {
        return active;
    }
    return strongest;
}

inline bool ShouldCreateZone(bool hasAnyZones, bool controlHeldAtMouseDown) {
    return !hasAnyZones || controlHeldAtMouseDown;
}

inline bool ShouldPreserveBasePresentation(
    bool hoverSampleRequested,
    bool targetOutlineActive,
    bool targetPreviewEnabled,
    bool interactionEditing) {
    return hoverSampleRequested ||
        (targetOutlineActive && targetPreviewEnabled &&
            !interactionEditing);
}

inline bool OverlayMatchesPresentation(
    bool targetOutline,
    std::uint64_t overlayRenderGeneration,
    std::uint64_t viewportRenderGeneration,
    std::uint64_t overlayTargetPreviewGeneration,
    std::uint64_t currentTargetPreviewGeneration) {
    return targetOutline
        ? overlayTargetPreviewGeneration > 0 &&
            overlayTargetPreviewGeneration <= currentTargetPreviewGeneration
        : overlayRenderGeneration == viewportRenderGeneration;
}

} // namespace Stack::RawLocalRangeTargetInteraction
