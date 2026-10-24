// SPDX-License-Identifier: GPL-3.0-only
#include "ScalarChannel.h"
#include "AnimationInternals.h"
#include <algorithm>
#include <cmath>
#include <limits>

namespace Stack::Animation {
namespace {
float Parameter(float value, float fallback, float minimum, float maximum) {
    return std::clamp(std::isfinite(value) ? value : fallback, minimum, maximum);
}
MotionSpec Sanitize(MotionSpec spec) {
    switch (spec.kind) {
    case MotionKind::Tween: case MotionKind::Exponential: case MotionKind::Spring: break;
    default: spec.kind = MotionKind::Exponential; break;
    }
    switch (spec.easing) {
    case Easing::Linear: case Easing::SmoothStep: case Easing::EaseIn:
    case Easing::EaseOut: case Easing::EaseInOut: break;
    default: spec.easing = Easing::SmoothStep; break;
    }
    spec.durationSeconds = Parameter(spec.durationSeconds, .2f, 1.f / 240.f, 3600.f);
    spec.rate = Parameter(spec.rate, 22.f, .001f, 1000.f);
    spec.frequencyHz = Parameter(spec.frequencyHz, 5.f, .01f, 120.f);
    spec.dampingRatio = Parameter(spec.dampingRatio, 1.f, .05f, 4.f);
    spec.settleEpsilon = Parameter(spec.settleEpsilon, .001f, 1.e-6f, 1.f);
    spec.velocityEpsilon = Parameter(spec.velocityEpsilon, .01f, 1.e-6f, 1000.f);
    return spec;
}
float FiniteFloat(double value) {
    constexpr double limit = std::numeric_limits<float>::max();
    return static_cast<float>(std::clamp(value, -limit, limit));
}
double Ease(double t, Easing easing) {
    switch (easing) {
    case Easing::Linear: return t;
    case Easing::EaseIn: return t * t;
    case Easing::EaseOut: return 1. - (1. - t) * (1. - t);
    case Easing::EaseInOut: return t < .5 ? 2. * t * t : 1. - 2. * (1. - t) * (1. - t);
    case Easing::SmoothStep: return t * t * (3. - 2. * t);
    }
    return t;
}
double EaseDerivative(double t, Easing easing) {
    switch (easing) {
    case Easing::Linear: return 1.;
    case Easing::EaseIn: return 2. * t;
    case Easing::EaseOut: return 2. * (1. - t);
    case Easing::EaseInOut: return t < .5 ? 4. * t : 4. * (1. - t);
    case Easing::SmoothStep: return 6. * t * (1. - t);
    }
    return 1.;
}
} // namespace

ScalarChannel::ScalarChannel(float initial) { Reset(initial); }
TrackSnapshot ScalarChannel::Snapshot() const {
    return {Value(), target_, Velocity(), active_, generation_};
}
void ScalarChannel::Reset(float value) {
    if (!std::isfinite(value)) value = 0.f;
    value_ = target_ = runStart_ = runEnd_ = segmentStart_ = value;
    velocity_ = 0.f;
    springDisplacement_ = 0.;
    elapsed_ = 0.;
    traversal_ = repeatsRemaining_ = 0;
    active_ = false;
    destinationIsEnd_ = true;
    // Retained generations are monotonic even across an owner's visual resets.
    if (generation_ != std::numeric_limits<std::uint64_t>::max()) ++generation_;
}
void ScalarChannel::Snap(float value) { SnapImpl(value, nullptr); }
bool ScalarChannel::Retarget(float target, const MotionSpec& spec, const Playback& playback) {
    return RetargetImpl(target, spec, playback, false, nullptr);
}
bool ScalarChannel::Replay(float target, const MotionSpec& spec, const Playback& playback) {
    return RetargetImpl(target, spec, playback, true, nullptr);
}
bool ScalarChannel::SpringTo(float target, float initialVelocity, const MotionSpec& spec,
    const Playback& playback) {
    return SpringToImpl(target, initialVelocity, spec, playback, nullptr);
}
bool ScalarChannel::Reverse() { return ReverseImpl(nullptr); }
void ScalarChannel::Cancel() { CancelImpl(CancelReason::Requested, nullptr); }
void ScalarChannel::Finish() { FinishImpl(nullptr); }
void ScalarChannel::Advance(double deltaSeconds, bool reducedMotion) {
    AdvanceImpl(deltaSeconds, reducedMotion, nullptr);
}
void ScalarChannel::Emit(EventKind kind, Detail::EventBatch* events, CancelReason reason) const {
    if (!events) return;
    AnimationEvent event;
    event.generation = generation_;
    event.kind = kind;
    event.cancelReason = reason;
    event.value = value_;
    event.from = segmentStart_;
    event.target = target_;
    event.traversal = traversal_;
    events->Push(event);
}
bool ScalarChannel::RetargetImpl(float target, const MotionSpec& spec,
    const Playback& playback, bool replay, Detail::EventBatch* events) {
    if (!std::isfinite(target) || (!replay && target == target_)) return false;
    const auto sanitized = Sanitize(spec);
    const double retainedVelocity = active_ && spec_.kind == MotionKind::Spring &&
        sanitized.kind == MotionKind::Spring ? velocity_ : 0.f;
    return BeginTransition(target, sanitized, playback, retainedVelocity, events);
}
bool ScalarChannel::SpringToImpl(float target, float initialVelocity, const MotionSpec& spec,
    const Playback& playback, Detail::EventBatch* events) {
    if (!std::isfinite(target) || !std::isfinite(initialVelocity) || spec.kind != MotionKind::Spring)
        return false;
    return BeginTransition(target, Sanitize(spec), playback, initialVelocity, events);
}
bool ScalarChannel::BeginTransition(float target, const MotionSpec& sanitized,
    const Playback& playback, double initialVelocity, Detail::EventBatch* events) {
    if (generation_ == std::numeric_limits<std::uint64_t>::max()) return false;
    CancelImpl(CancelReason::Retargeted, events);
    ++generation_;
    spec_ = sanitized;
    playback_ = playback;
    playback_.repeatCount = std::min<std::uint32_t>(playback.repeatCount, 32);
    if (playback_.repeatMode != RepeatMode::PingPong) playback_.repeatMode = RepeatMode::Restart;
    runStart_ = segmentStart_ = value_;
    runEnd_ = target_ = target;
    velocity_ = initialVelocity;
    springDisplacement_ = value_ - target_;
    elapsed_ = 0.;
    traversal_ = 0;
    repeatsRemaining_ = playback_.repeatCount;
    destinationIsEnd_ = true;
    active_ = true;
    Emit(EventKind::Started, events);
    if (value_ == target_ && velocity_ == 0.f) FinishImpl(events);
    return true;
}
bool ScalarChannel::ReverseImpl(Detail::EventBatch* events) {
    if (runStart_ == runEnd_ || generation_ == std::numeric_limits<std::uint64_t>::max()) return false;
    const double retainedVelocity = spec_.kind == MotionKind::Spring ? velocity_ : 0.f;
    CancelImpl(CancelReason::Retargeted, events);
    ++generation_;
    destinationIsEnd_ = !destinationIsEnd_;
    target_ = destinationIsEnd_ ? runEnd_ : runStart_;
    segmentStart_ = value_;
    velocity_ = retainedVelocity;
    springDisplacement_ = value_ - target_;
    elapsed_ = 0.;
    traversal_ = 0;
    repeatsRemaining_ = playback_.repeatCount;
    active_ = true;
    Emit(EventKind::Started, events);
    if (value_ == target_ && velocity_ == 0.f) FinishImpl(events);
    return true;
}
void ScalarChannel::CancelImpl(CancelReason reason, Detail::EventBatch* events) {
    if (!active_) return;
    active_ = false;
    velocity_ = 0.f;
    repeatsRemaining_ = 0;
    Emit(EventKind::Cancelled, events, reason);
}
void ScalarChannel::SnapImpl(float value, Detail::EventBatch* events) {
    if (!std::isfinite(value)) return;
    CancelImpl(CancelReason::Snapped, events);
    if (generation_ != std::numeric_limits<std::uint64_t>::max()) ++generation_;
    value_ = target_ = runStart_ = runEnd_ = segmentStart_ = value;
    velocity_ = 0.f;
    springDisplacement_ = 0.;
    elapsed_ = 0.;
    destinationIsEnd_ = true;
}
void ScalarChannel::FinishImpl(Detail::EventBatch* events) {
    if (!active_) return;
    value_ = target_;
    velocity_ = 0.f;
    springDisplacement_ = 0.;
    repeatsRemaining_ = 0;
    Emit(destinationIsEnd_ ? EventKind::ReachedEnd : EventKind::ReachedStart, events);
    active_ = false;
    Emit(EventKind::Completed, events);
}
void ScalarChannel::ReachEndpoint(Detail::EventBatch* events) {
    value_ = target_;
    velocity_ = 0.f;
    springDisplacement_ = 0.;
    Emit(destinationIsEnd_ ? EventKind::ReachedEnd : EventKind::ReachedStart, events);
    if (!repeatsRemaining_) {
        active_ = false;
        Emit(EventKind::Completed, events);
        return;
    }
    --repeatsRemaining_;
    ++traversal_;
    if (playback_.repeatMode == RepeatMode::PingPong) destinationIsEnd_ = !destinationIsEnd_;
    target_ = destinationIsEnd_ ? runEnd_ : runStart_;
    segmentStart_ = destinationIsEnd_ ? runStart_ : runEnd_;
    value_ = segmentStart_;
    springDisplacement_ = value_ - target_;
    elapsed_ = 0.;
    Emit(EventKind::Repeated, events);
}
void ScalarChannel::AdvanceSpring(double dt) {
    // Exact solution of x'' + 2*zeta*omega*x' + omega^2*x = 0.
    // Keeping the solve in double prevents intermediate overflow for finite floats.
    constexpr double pi = 3.14159265358979323846;
    const double omega = 2. * pi * spec_.frequencyHz;
    const double zeta = spec_.dampingRatio;
    const double x = springDisplacement_;
    const double v = velocity_;
    double nextX = 0., nextV = 0.;
    if (std::abs(zeta - 1.) < 1.e-5) {
        const double decay = std::exp(-omega * dt);
        const double b = v + omega * x;
        nextX = (x + b * dt) * decay;
        nextV = (v - omega * b * dt) * decay;
    } else if (zeta < 1.) {
        const double decay = std::exp(-zeta * omega * dt);
        const double damped = omega * std::sqrt(1. - zeta * zeta);
        const double sine = std::sin(damped * dt);
        const double cosine = std::cos(damped * dt);
        const double b = (v + zeta * omega * x) / damped;
        nextX = decay * (x * cosine + b * sine);
        nextV = decay * (v * cosine - (zeta * omega * b + damped * x) * sine);
    } else {
        const double root = std::sqrt(zeta * zeta - 1.);
        const double r1 = -omega * (zeta - root);
        const double r2 = -omega * (zeta + root);
        const double c1 = (v - r2 * x) / (r1 - r2);
        const double c2 = x - c1;
        const double a = c1 * std::exp(r1 * dt);
        const double b = c2 * std::exp(r2 * dt);
        nextX = a + b;
        nextV = r1 * a + r2 * b;
    }
    const double nextValue = static_cast<double>(target_) + nextX;
    if (!std::isfinite(nextValue) || !std::isfinite(nextV)) {
        value_ = target_;
        velocity_ = 0.f;
        springDisplacement_ = 0.;
        return;
    }
    constexpr double limit = std::numeric_limits<float>::max();
    springDisplacement_ = nextX;
    value_ = std::clamp(nextValue, -limit, limit);
    velocity_ = std::clamp(nextV, -limit, limit);
}
void ScalarChannel::AdvanceImpl(double deltaSeconds, bool reducedMotion, Detail::EventBatch* events) {
    if (!active_) return;
    if (reducedMotion) { FinishImpl(events); return; }
    if (!std::isfinite(deltaSeconds) || deltaSeconds <= 0.) return;
    double remaining = std::min(deltaSeconds, .25);
    while (active_ && remaining > 0.) {
        if (spec_.kind == MotionKind::Tween) {
            const double duration = spec_.durationSeconds;
            const double consumed = std::min(remaining, std::max(0., duration - elapsed_));
            elapsed_ += consumed;
            remaining -= consumed;
            const double t = std::clamp(elapsed_ / duration, 0., 1.);
            const double span = static_cast<double>(target_) - segmentStart_;
            value_ = FiniteFloat(static_cast<double>(segmentStart_) + span * Ease(t, spec_.easing));
            velocity_ = FiniteFloat(span * EaseDerivative(t, spec_.easing) / duration);
            if (elapsed_ >= duration) ReachEndpoint(events);
            else break;
        } else if (spec_.kind == MotionKind::Exponential) {
            const double difference = static_cast<double>(segmentStart_) - target_;
            const double distance = std::abs(difference);
            const double settleTime = distance <= spec_.settleEpsilon ? 0. :
                std::log(distance / spec_.settleEpsilon) / spec_.rate;
            const double consumed = std::min(remaining, std::max(0., settleTime - elapsed_));
            elapsed_ += consumed;
            // Integrate displacement from the fixed segment origin, not rounded
            // absolute output. Even double absolute accumulation can strand a
            // small residual next to a large target forever.
            const double displacement = difference * std::exp(-spec_.rate * elapsed_);
            value_ = static_cast<double>(target_) + displacement;
            constexpr double limit = std::numeric_limits<float>::max();
            velocity_ = std::clamp(-spec_.rate * displacement, -limit, limit);
            remaining -= consumed;
            if (elapsed_ >= settleTime) ReachEndpoint(events);
            else break;
        } else {
            AdvanceSpring(remaining);
            remaining = 0.;
            if (std::abs(springDisplacement_) <= spec_.settleEpsilon &&
                std::abs(velocity_) <= spec_.velocityEpsilon) ReachEndpoint(events);
        }
    }
}
std::size_t ScalarChannel::ReservedEvents() const {
    return active_ ? 2 + 2 * static_cast<std::size_t>(repeatsRemaining_) : 0;
}
} // namespace Stack::Animation
