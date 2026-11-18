#pragma once

namespace Stack::Hdr {
// One compatible-transfer PBO is in flight at a time. During interaction its
// readback shares the comparison fence, avoiding a second UI polling turn.
enum class CompatibleTransferPhase { Ready, Comparing, Reading, ComparingAndReading };

struct CompatibleTransferCompletion {
    CompatibleTransferPhase next = CompatibleTransferPhase::Ready;
    bool copyPixels = false;
};

constexpr CompatibleTransferPhase BeginCompatibleTransfer(bool interacting) {
    return interacting ? CompatibleTransferPhase::ComparingAndReading
                       : CompatibleTransferPhase::Comparing;
}

constexpr bool CompatibleTransferNeedsReadback(CompatibleTransferPhase phase) {
    return phase == CompatibleTransferPhase::Reading ||
           phase == CompatibleTransferPhase::ComparingAndReading;
}

// Called only after the fence signals. A combined readback of an unchanged
// frame never reaches the CPU or D3D copy, even though the GPU filled the PBO.
constexpr CompatibleTransferCompletion CompleteCompatibleTransfer(
    CompatibleTransferPhase phase, bool changed) {
    if (phase == CompatibleTransferPhase::Comparing)
        return {changed ? CompatibleTransferPhase::Reading : CompatibleTransferPhase::Ready, false};
    return {CompatibleTransferPhase::Ready,
        phase == CompatibleTransferPhase::Reading ||
        (phase == CompatibleTransferPhase::ComparingAndReading && changed)};
}
}
