// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include "AnimationTypes.h"
#include <cstddef>

namespace Stack::Animation {
namespace Detail { struct EventBatch; }
class AnimationController;

// Cheap value-owned scalar. Compose channels for positions, sizes, colors,
// glass response or any other shape-independent presentation values. No clock,
// rendering dependency, allocation, callbacks or application actions live here.
class ScalarChannel {
public:
    explicit ScalarChannel(float initial = 0.f);
    float Value() const { return static_cast<float>(value_); }
    float Target() const { return target_; }
    float Velocity() const { return static_cast<float>(velocity_); }
    bool IsActive() const { return active_; }
    std::uint64_t Generation() const { return generation_; }
    TrackSnapshot Snapshot() const;

    // Reset is silent lifetime initialization; Snap cancels an active transition.
    void Reset(float value);
    void Snap(float value);
    // Same target is a no-op even with changed spec/playback or after Cancel.
    // Replay explicitly restarts and applies changed settings.
    bool Retarget(float target, const MotionSpec& spec, const Playback& playback = {});
    bool Replay(float target, const MotionSpec& spec, const Playback& playback = {});
    // Explicit gesture handoff: signed value-units/second, never divided by
    // distance. Always starts a new generation, even at the same target. Value
    // stays continuous. Rejects nonfinite inputs and non-Spring specs. Supplied
    // velocity applies to the first traversal; repeated traversals start at rest.
    bool SpringTo(float target, float initialVelocity,
        const MotionSpec& spec = MotionSpec::Spring(), const Playback& playback = {});
    // Reverse current/settled direction toward the opposite original endpoint,
    // cancelling the old generation and restarting the full repeat policy.
    bool Reverse();
    // Cancel holds the current value; Replay is required to resume that target.
    void Cancel();
    // Finish reaches the CURRENT destination, discarding remaining repeats.
    void Finish();
    // Invalid/nonpositive dt does not advance. Finite dt is capped at .25s to
    // avoid resume jumps. Reduced motion finishes once even when dt is zero.
    void Advance(double deltaSeconds, bool reducedMotion = false);

private:
    friend class AnimationController;
    bool RetargetImpl(float target, const MotionSpec& spec, const Playback& playback,
        bool replay, Detail::EventBatch* events);
    bool SpringToImpl(float target, float initialVelocity, const MotionSpec& spec,
        const Playback& playback, Detail::EventBatch* events);
    bool BeginTransition(float target, const MotionSpec& sanitized, const Playback& playback,
        double initialVelocity, Detail::EventBatch* events);
    bool ReverseImpl(Detail::EventBatch* events);
    void CancelImpl(CancelReason reason, Detail::EventBatch* events);
    void SnapImpl(float value, Detail::EventBatch* events);
    void FinishImpl(Detail::EventBatch* events);
    void AdvanceImpl(double deltaSeconds, bool reducedMotion, Detail::EventBatch* events);
    void Emit(EventKind kind, Detail::EventBatch* events,
        CancelReason reason = CancelReason::None) const;
    void ReachEndpoint(Detail::EventBatch* events);
    void AdvanceSpring(double dt);
    std::size_t ReservedEvents() const;

    MotionSpec spec_;
    Playback playback_;
    double value_ = 0.;
    float target_ = 0.f;
    double velocity_ = 0.;
    double springDisplacement_ = 0.;
    float runStart_ = 0.f;
    float runEnd_ = 0.f;
    float segmentStart_ = 0.f;
    double elapsed_ = 0.;
    std::uint64_t generation_ = 0;
    std::uint32_t traversal_ = 0;
    std::uint32_t repeatsRemaining_ = 0;
    bool active_ = false;
    bool destinationIsEnd_ = true;
};

} // namespace Stack::Animation
