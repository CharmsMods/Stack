// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <cstdint>

namespace Stack::Animation {

enum class MotionKind { Tween, Exponential, Spring };
enum class Easing { Linear, SmoothStep, EaseIn, EaseOut, EaseInOut };
enum class RepeatMode { Restart, PingPong };

// Parameters are sanitized at the channel boundary. Motion is presentation only.
struct MotionSpec {
    MotionKind kind = MotionKind::Exponential;
    Easing easing = Easing::SmoothStep;
    float durationSeconds = .2f;
    float rate = 22.f;
    float frequencyHz = 5.f;
    float dampingRatio = 1.f;
    float settleEpsilon = .001f;
    float velocityEpsilon = .01f;
    static MotionSpec Tween(float durationSeconds, Easing easing = Easing::SmoothStep);
    static MotionSpec Exponential(float rate, float settleEpsilon = .01f);
    static MotionSpec Spring(float frequencyHz = 5.f, float dampingRatio = 1.f,
        float settleEpsilon = .001f);
};

// Additional traversals, not total plays. Bounded to 32. PingPong alternates
// exact start/end destinations; Restart resets to the original start each time.
struct Playback {
    std::uint32_t repeatCount = 0;
    RepeatMode repeatMode = RepeatMode::Restart;
};

struct OwnerId {
    std::uint64_t value = 0;
    std::uint64_t scope = 0;
    explicit operator bool() const { return value != 0 && scope != 0; }
};
struct TrackId {
    std::uint64_t value = 0;
    std::uint64_t scope = 0;
    explicit operator bool() const { return value != 0 && scope != 0; }
};
inline bool operator==(OwnerId a, OwnerId b) { return a.value == b.value && a.scope == b.scope; }
inline bool operator!=(OwnerId a, OwnerId b) { return !(a == b); }
inline bool operator==(TrackId a, TrackId b) { return a.value == b.value && a.scope == b.scope; }
inline bool operator!=(TrackId a, TrackId b) { return !(a == b); }

enum class EventKind { Started, ReachedStart, ReachedEnd, Repeated, Completed, Cancelled };
enum class CancelReason { None, Requested, Retargeted, Snapped };

// Started records departure/entry intent (from -> target); ReachedStart/End are
// presentation endpoint arrival, not authored state changes or input actions.
struct AnimationEvent {
    OwnerId owner;
    TrackId track;
    std::uint64_t generation = 0;
    EventKind kind = EventKind::Started;
    CancelReason cancelReason = CancelReason::None;
    float value = 0.f;
    float from = 0.f;
    float target = 0.f;
    std::uint32_t traversal = 0;
};

struct TrackSnapshot {
    float value = 0.f;
    float target = 0.f;
    float velocity = 0.f;
    bool active = false;
    std::uint64_t generation = 0;
};

} // namespace Stack::Animation
