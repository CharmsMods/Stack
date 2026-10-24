// SPDX-License-Identifier: GPL-3.0-only
#include "Animation/AnimationController.h"
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <vector>

namespace A = Stack::Animation;
namespace {
void Require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
void Near(float actual, float expected, float epsilon, const char* message) {
    if (!std::isfinite(actual) || std::abs(actual - expected) > epsilon)
        throw std::runtime_error(message);
}
std::vector<A::AnimationEvent> Drain(A::AnimationController& controller) {
    std::vector<A::AnimationEvent> events(controller.PendingEventCount());
    Require(controller.DrainEvents(events.data(), events.size()) == events.size(), "drain count");
    return events;
}
std::size_t Count(const std::vector<A::AnimationEvent>& events, A::EventKind kind) {
    std::size_t result = 0;
    for (const auto& event : events) if (event.kind == kind) ++result;
    return result;
}
void ScalarTimingAndLegacyRates() {
    for (const float rate : {26.f, 30.f, 22.f}) {
        A::ScalarChannel channel;
        Require(channel.Retarget(1.f, A::MotionSpec::Exponential(rate)), "exponential start");
        channel.Advance(1. / 60.);
        Near(channel.Value(), static_cast<float>(1. - std::exp(-rate / 60.)), 1.e-6f, "legacy rate");
        const auto generation = channel.Generation();
        const auto value = channel.Value();
        Require(!channel.Retarget(1.f, A::MotionSpec::Tween(.1f)), "same target restarted");
        Require(channel.Generation() == generation && channel.Value() == value, "idempotent retarget mutated");
        for (int i = 0; i < 90; ++i) channel.Advance(1. / 60.);
        Require(channel.Value() == 1.f && !channel.IsActive(), "exponential exact settlement");
    }
    A::ScalarChannel a, b;
    a.Retarget(1.f, A::MotionSpec::Exponential(2.f, 1.e-6f));
    b.Retarget(1.f, A::MotionSpec::Exponential(2.f, 1.e-6f));
    a.Advance(.2);
    for (int i = 0; i < 20; ++i) b.Advance(.01);
    Near(a.Value(), b.Value(), 1.e-6f, "exponential subdivision");
    A::ScalarChannel press;
    press.Snap(1.f);
    Require(press.Value() == 1.f && !press.IsActive(), "immediate press");
    press.Retarget(0.f, A::MotionSpec::Exponential(30.f));
    press.Advance(1. / 60.);
    Near(press.Value(), std::exp(-.5f), 1.e-6f, "press release");
}
void ExponentialSubUlpProgress() {
    for (const float sign : {1.f, -1.f}) {
        A::AnimationController controller;
        const auto owner = controller.CreateOwner();
        const auto track = controller.CreateTrack(owner, sign * 999999.f);
        controller.Retarget(owner, track, sign * 1000000.f, A::MotionSpec::Exponential(1.f, 1.e-6f));
        for (int i = 0; i < 2000; ++i) controller.Advance(.01);
        const auto state = controller.Read(owner, track);
        Require(state && state->value == sign * 1000000.f && !state->active,
            "sub-ULP exponential progress stalled");
        const auto events = Drain(controller);
        Require(Count(events, A::EventKind::Completed) == 1 && events.size() == 3,
            "sub-ULP exponential completion missing");
    }
}
void FullRangeRelativeSettlement() {
    const float maximum = std::numeric_limits<float>::max();
    for (const auto spec : {A::MotionSpec::Exponential(1.f, 1.e-6f),
        A::MotionSpec::Spring(2.f, .3f, 1.e-6f), A::MotionSpec::Spring(2.f, 1.f, 1.e-6f),
        A::MotionSpec::Spring(2.f, 2.f, 1.e-6f)}) {
        for (const float target : {1.e12f, maximum, -maximum, std::numeric_limits<float>::denorm_min()}) {
            A::ScalarChannel channel(target == maximum ? -maximum : 0.f);
            channel.Retarget(target, spec);
            for (int i = 0; i < 12000 && channel.IsActive(); ++i) {
                channel.Advance(.01);
                Require(std::isfinite(channel.Value()) && std::isfinite(channel.Velocity()),
                    "full-range motion escaped finite output bounds");
            }
            Require(channel.Value() == target && !channel.IsActive() && channel.Velocity() == 0.f,
                "full-range relative motion failed to settle");
        }
    }
}
void TweenReverseRepeatAndComposition() {
    A::ScalarChannel channel;
    channel.Retarget(10.f, A::MotionSpec::Tween(.2f, A::Easing::Linear));
    channel.Advance(.1);
    Near(channel.Value(), 5.f, 1.e-5f, "tween midpoint");
    Require(channel.Reverse(), "reverse active");
    channel.Advance(.25);
    Require(channel.Value() == 0.f && !channel.IsActive(), "reverse exact start");
    Require(channel.Reverse(), "reverse settled");
    channel.Advance(.25);
    Require(channel.Value() == 10.f, "reverse settled destination");
    channel.Reset(0.f);
    channel.Retarget(1.f, A::MotionSpec::Tween(.05f, A::Easing::Linear), {3, A::RepeatMode::PingPong});
    channel.Advance(.25);
    Require(channel.Value() == 0.f && !channel.IsActive(), "ping pong traversal count");
    channel.Replay(1.f, A::MotionSpec::Tween(.05f), {2, A::RepeatMode::Restart});
    channel.Advance(.25);
    Require(channel.Value() == 1.f && !channel.IsActive(), "restart traversal count");
    A::ScalarChannel positionX(-50.f), positionY(10.f), radius(4.f);
    positionX.Retarget(200.f, A::MotionSpec::Tween(.1f));
    positionY.Retarget(-100.f, A::MotionSpec::Tween(.1f));
    radius.Retarget(30.f, A::MotionSpec::Tween(.1f));
    for (auto* scalar : {&positionX, &positionY, &radius}) scalar->Advance(.2);
    Require(positionX.Value() == 200.f && positionY.Value() == -100.f && radius.Value() == 30.f,
        "geometry-independent composition");
}
void SpringSolutionsAndRetargeting() {
    for (const float damping : {.3f, 1.f, 2.f}) {
        auto spec = A::MotionSpec::Spring(2.f, damping, 1.e-6f);
        spec.velocityEpsilon = 1.e-6f;
        A::ScalarChannel a, b;
        a.Retarget(10.f, spec);
        b.Retarget(10.f, spec);
        a.Advance(.2);
        for (int i = 0; i < 20; ++i) b.Advance(.01);
        Near(a.Value(), b.Value(), 2.e-5f, "spring subdivision value");
        Near(a.Velocity(), b.Velocity(), 5.e-5f, "spring subdivision velocity");
        const float velocity = a.Velocity();
        const float value = a.Value();
        a.Retarget(-5.f, spec);
        Require(a.Value() == value && a.Velocity() == velocity, "spring retarget continuity");
        for (int i = 0; i < 3000 && a.IsActive(); ++i) a.Advance(.01);
        Require(a.Value() == -5.f && a.Velocity() == 0.f && !a.IsActive(), "spring settles");
    }
    auto spec = A::MotionSpec::Spring(2.f, .05f, 1.f);
    A::ScalarChannel crossing;
    crossing.Retarget(1.f, spec);
    crossing.Advance(.125);
    Require(crossing.IsActive() && std::abs(crossing.Velocity()) > spec.velocityEpsilon,
        "spring falsely completed while crossing target");
}
void InvalidInputsAndReducedMotion() {
    const auto nan = std::numeric_limits<float>::quiet_NaN();
    const auto inf = std::numeric_limits<float>::infinity();
    A::ScalarChannel channel(nan);
    Require(channel.Value() == 0.f, "invalid initial value");
    Require(!channel.Retarget(nan, A::MotionSpec{}), "invalid target accepted");
    channel.Retarget(5.f, A::MotionSpec::Tween(1.f, A::Easing::Linear));
    const auto generation = channel.Generation();
    for (double dt : {0., -1., static_cast<double>(nan), static_cast<double>(inf)}) {
        channel.Advance(dt);
        Require(channel.Value() == 0.f && channel.IsActive(), "invalid dt advanced");
    }
    channel.Snap(inf);
    Require(channel.Generation() == generation && channel.Value() == 0.f, "invalid snap mutated");
    channel.Advance(1000000.);
    Near(channel.Value(), 1.25f, 1.e-6f, "large dt clamp");
    channel.Advance(0., true);
    Require(channel.Value() == 5.f && !channel.IsActive(), "reduced motion zero dt");
    for (auto spec : {A::MotionSpec::Tween(nan), A::MotionSpec::Tween(-1.f),
        A::MotionSpec::Exponential(inf, nan), A::MotionSpec::Spring(nan, -10.f, nan)}) {
        channel.Reset(0.f);
        channel.Retarget(1.f, spec, {std::numeric_limits<std::uint32_t>::max(), A::RepeatMode::Restart});
        for (int i = 0; i < 10; ++i) channel.Advance(.25);
        Require(std::isfinite(channel.Value()) && std::isfinite(channel.Velocity()), "invalid spec escaped finite guard");
        channel.Finish();
        Require(channel.Value() == channel.Target() && !channel.IsActive(), "finish sanitized motion");
    }
    channel.Reset(-std::numeric_limits<float>::max());
    channel.Retarget(std::numeric_limits<float>::max(), A::MotionSpec::Spring(120.f, .05f));
    for (int i = 0; i < 10; ++i) channel.Advance(.01);
    Require(std::isfinite(channel.Value()) && std::isfinite(channel.Velocity()), "extreme finite values overflowed");
}
void QueuedLifecycleAndReentry() {
    A::AnimationController controller;
    const auto owner = controller.CreateOwner();
    const auto track = controller.CreateTrack(owner);
    const auto spec = A::MotionSpec::Tween(.1f, A::Easing::Linear);
    Require(controller.Retarget(owner, track, 1.f, spec) == A::RequestResult::Accepted, "observed start");
    auto events = Drain(controller);
    Require(events.size() == 1 && events[0].kind == A::EventKind::Started, "single queued start");
    const auto old = events[0];
    controller.Advance(.05);
    Require(controller.Retarget(owner, track, 1.f, A::MotionSpec::Spring()) == A::RequestResult::Unchanged,
        "observed same target restarted");
    Require(controller.PendingEventCount() == 0, "same target produced event");
    controller.Retarget(owner, track, -1.f, spec);
    events = Drain(controller);
    Require(events.size() == 2 && events[0].kind == A::EventKind::Cancelled &&
        events[0].cancelReason == A::CancelReason::Retargeted && events[0].generation == old.generation &&
        events[1].kind == A::EventKind::Started && events[1].generation != old.generation,
        "retarget generation/order");
    Require(!controller.IsCurrent(old) && controller.IsCurrent(events[1]), "generation guards");
    Near(events[1].from, .5f, 1.e-5f, "transition departure value");
    controller.Advance(.25);
    events = Drain(controller);
    Require(events.size() == 2 && events[0].kind == A::EventKind::ReachedEnd &&
        events[1].kind == A::EventKind::Completed && events[1].value == -1.f, "completion events");
    controller.Advance(.25);
    Require(controller.PendingEventCount() == 0, "duplicate completion");
    // Dispatching this already-drained completion can safely create a NEW run.
    controller.Replay(owner, track, -1.f, spec);
    Require(events.size() == 2 && controller.PendingEventCount() == 3, "drain reentry lost new events");
    auto replay = Drain(controller);
    Require(Count(replay, A::EventKind::Started) == 1 && Count(replay, A::EventKind::Completed) == 1,
        "zero-distance replay lifecycle");
    controller.Snap(owner, track, 3.f);
    Require(!controller.IsCurrent(replay.back()), "snap failed to invalidate drained completion");
    controller.Retarget(owner, track, 9.f, spec);
    const auto activeStart = Drain(controller).front();
    controller.Snap(owner, track, 4.f);
    const auto snapped = Drain(controller);
    Require(snapped.size() == 1 && snapped.front().kind == A::EventKind::Cancelled &&
        snapped.front().cancelReason == A::CancelReason::Snapped &&
        snapped.front().generation == activeStart.generation && !controller.IsCurrent(snapped.front()),
        "active snap cancellation identity");
}
void ObservedRepeatsReverseAndCancellation() {
    A::AnimationController controller;
    const auto owner = controller.CreateOwner();
    const auto track = controller.CreateTrack(owner);
    const auto spec = A::MotionSpec::Tween(.02f, A::Easing::Linear);
    controller.Retarget(owner, track, 1.f, spec, {3, A::RepeatMode::PingPong});
    controller.Advance(.25);
    auto events = Drain(controller);
    Require(Count(events, A::EventKind::Started) == 1 && Count(events, A::EventKind::Repeated) == 3 &&
        Count(events, A::EventKind::ReachedEnd) == 2 && Count(events, A::EventKind::ReachedStart) == 2 &&
        Count(events, A::EventKind::Completed) == 1, "repeat lifecycle counts");
    Require(events.back().traversal == 3 && events.back().value == 0.f, "repeat final endpoint");
    controller.Reverse(owner, track);
    controller.Advance(0., true);
    events = Drain(controller);
    Require(Count(events, A::EventKind::Completed) == 1 && Count(events, A::EventKind::Repeated) == 0 &&
        events.back().value == 1.f, "reduced motion ignored current reversed destination");
    controller.Replay(owner, track, 0.f, spec, {32, A::RepeatMode::PingPong});
    controller.Cancel(owner, track);
    Require(controller.Cancel(owner, track) == A::RequestResult::Unchanged, "duplicate cancel accepted");
    controller.Advance(.25);
    events = Drain(controller);
    Require(Count(events, A::EventKind::Cancelled) == 1 && Count(events, A::EventKind::Completed) == 0,
        "cancel emitted completion or duplicate");
    Require(controller.Retarget(owner, track, 0.f, spec) == A::RequestResult::Unchanged,
        "same target resumed cancelled run");
}
void CapacityLifetimeAndIsolation() {
    A::AnimationController controller({2, 2, 3});
    const auto a = controller.CreateOwner();
    const auto b = controller.CreateOwner();
    Require(!controller.CreateOwner(), "owner capacity exceeded");
    const auto x = controller.CreateTrack(a);
    const auto y = controller.CreateTrack(b, 7.f);
    Require(!controller.CreateTrack(a), "track capacity exceeded");
    const auto spec = A::MotionSpec::Tween(.1f);
    Require(controller.Retarget(a, x, 1.f, spec) == A::RequestResult::Accepted, "minimal reservation");
    Require(controller.Retarget(b, y, 8.f, spec) == A::RequestResult::EventCapacityExceeded,
        "queue admission exceeded");
    Require(controller.Read(b, y)->value == 7.f && !controller.Read(b, y)->active, "rejection mutated track");
    Require(controller.Retarget(a, x, 2.f, spec) == A::RequestResult::EventCapacityExceeded,
        "retarget over reservation accepted");
    controller.Advance(.25);
    Require(controller.Read(a, x)->value == 1.f && !controller.Read(a, x)->active,
        "full queue stalled accepted visual motion");
    Require(controller.PendingEventCount() == 3, "reservation lost terminal events");
    A::AnimationEvent first;
    Require(controller.DrainEvents(nullptr, 5) == 0 && controller.DrainEvents(&first, 0) == 0,
        "invalid drain changed queue");
    Require(controller.DrainEvents(&first, 1) == 1 && first.kind == A::EventKind::Started,
        "partial drain FIFO");
    Require(controller.RemoveOwner(a), "owner removal");
    Require(controller.PendingEventCount() == 0 && controller.TrackCount() == 1 &&
        !controller.Read(a, x) && !controller.IsCurrent(first), "owner cleanup retained stale state");
    const auto replacement = controller.CreateOwner();
    const auto replacementTrack = controller.CreateTrack(replacement);
    Require(replacement != a && replacementTrack != x, "identity reused");
    Require(controller.Retarget(a, x, 9.f, spec) == A::RequestResult::InvalidHandle, "stale handle accepted");
    Require(controller.Retarget(replacement, y, 9.f, spec) == A::RequestResult::InvalidHandle,
        "cross-owner track accepted");
    Require(controller.Read(b, y)->value == 7.f, "other owner modified");
    controller.Retarget(replacement, replacementTrack, 2.f, spec);
    Require(controller.RemoveTrack(replacement, replacementTrack) && controller.PendingEventCount() == 0,
        "track cleanup retained events");
}
void ExplicitSpringVelocityHandoff() {
    constexpr double pi = 3.14159265358979323846;
    const auto spec = A::MotionSpec::Spring(2.f, 1.f, 1.e-6f);
    for (const float velocity : {-8.f, 8.f}) {
        A::ScalarChannel channel;
        channel.Snap(2.f); // The drag owns value updates before releasing.
        const auto generation = channel.Generation();
        Require(channel.SpringTo(5.f, velocity, spec), "spring handoff rejected");
        Require(channel.Value() == 2.f && channel.Velocity() == velocity &&
            channel.Generation() != generation && channel.IsActive(), "handoff initial continuity/units");
        const double dt = .01;
        const double omega = 2. * pi * 2.;
        const double x = -3.;
        const double b = velocity + omega * x;
        channel.Advance(dt);
        Near(channel.Value(), static_cast<float>(5. + (x + b * dt) * std::exp(-omega * dt)),
            1.e-6f, "signed initial velocity solution");
        Near(channel.Velocity(), static_cast<float>((velocity - omega * b * dt) * std::exp(-omega * dt)),
            1.e-5f, "signed initial velocity derivative");
        const float current = channel.Value();
        const float currentVelocity = channel.Velocity();
        Require(channel.Reverse() && channel.Value() == current && channel.Velocity() == currentVelocity,
            "handoff reverse discontinuity");
        for (int i = 0; i < 3000 && channel.IsActive(); ++i) channel.Advance(.01);
        Require(channel.Value() == 2.f && !channel.IsActive(), "handoff reverse endpoint");
    }
    for (const float velocity : {-10.f, 10.f}) {
        A::ScalarChannel channel(4.f);
        Require(channel.SpringTo(4.f, velocity, spec) && channel.IsActive(), "zero-distance handoff suppressed");
        channel.Advance(.01);
        Require((channel.Value() - 4.f) * velocity > 0.f, "zero-distance velocity sign lost");
        for (int i = 0; i < 3000 && channel.IsActive(); ++i) channel.Advance(.01);
        Require(channel.Value() == 4.f && !channel.IsActive(), "zero-distance handoff did not return");
    }
    A::ScalarChannel implicit, explicitZero;
    implicit.Retarget(10.f, spec);
    explicitZero.SpringTo(10.f, 0.f, spec);
    for (int i = 0; i < 100; ++i) {
        implicit.Advance(.01);
        explicitZero.Advance(.01);
        Require(implicit.Value() == explicitZero.Value() && implicit.Velocity() == explicitZero.Velocity() &&
            implicit.IsActive() == explicitZero.IsActive(), "zero-velocity handoff changed existing spring output");
    }
}
void SpringHandoffLifecycleRegrabAndGuards() {
    A::AnimationController controller;
    const auto owner = controller.CreateOwner();
    const auto track = controller.CreateTrack(owner);
    const auto spec = A::MotionSpec::Spring(2.f, .5f);
    Require(controller.SpringTo(owner, track, 1.f, 3.f, spec) == A::RequestResult::Accepted, "observed handoff");
    const auto oldStart = Drain(controller).front();
    controller.Advance(.02);
    const auto moving = *controller.Read(owner, track);
    Require(controller.SpringTo(owner, track, 1.f, -2.f, spec) == A::RequestResult::Accepted,
        "same-target handoff did not restart");
    auto events = Drain(controller);
    Require(events.size() == 2 && events[0].kind == A::EventKind::Cancelled &&
        events[0].generation == oldStart.generation && events[1].kind == A::EventKind::Started &&
        events[1].generation != oldStart.generation && controller.Read(owner, track)->value == moving.value &&
        controller.Read(owner, track)->velocity == -2.f, "explicit velocity replacement lifecycle");
    controller.Snap(owner, track, .7f); // Regrab: direct manipulation takes over immediately.
    events = Drain(controller);
    Require(events.size() == 1 && events[0].kind == A::EventKind::Cancelled &&
        events[0].cancelReason == A::CancelReason::Snapped && !controller.IsCurrent(events[0]), "regrab cancellation");
    Require(controller.Read(owner, track)->value == .7f && controller.Read(owner, track)->velocity == 0.f &&
        !controller.Read(owner, track)->active, "regrab did not stop physical motion");
    controller.SpringTo(owner, track, 1.f, -3.f, spec);
    Require(controller.Read(owner, track)->value == .7f && controller.Read(owner, track)->velocity == -3.f,
        "release after regrab lost supplied value/velocity");
    const auto beforeInvalid = *controller.Read(owner, track);
    const auto pending = controller.PendingEventCount();
    const float nan = std::numeric_limits<float>::quiet_NaN();
    const float inf = std::numeric_limits<float>::infinity();
    for (const float invalid : {nan, inf, -inf}) {
        Require(controller.SpringTo(owner, track, 1.f, invalid, spec) == A::RequestResult::InvalidValue,
            "invalid release velocity accepted");
        Require(controller.SpringTo(owner, track, invalid, 2.f, spec) == A::RequestResult::InvalidValue,
            "invalid release target accepted");
    }
    Require(controller.SpringTo(owner, track, 1.f, 2.f, A::MotionSpec::Tween(.1f)) == A::RequestResult::InvalidValue &&
        controller.SpringTo(owner, track, 1.f, 2.f, A::MotionSpec::Exponential(22.f)) == A::RequestResult::InvalidValue,
        "non-spring handoff accepted");
    const auto afterInvalid = *controller.Read(owner, track);
    Require(afterInvalid.generation == beforeInvalid.generation && afterInvalid.value == beforeInvalid.value &&
        afterInvalid.velocity == beforeInvalid.velocity && afterInvalid.target == beforeInvalid.target &&
        controller.PendingEventCount() == pending, "invalid handoff mutated state or events");
    controller.Cancel(owner, track);
    controller.Advance(.25);
    events = Drain(controller);
    Require(Count(events, A::EventKind::Cancelled) == 1 && Count(events, A::EventKind::Completed) == 0,
        "cancelled handoff completed");
    controller.SpringTo(owner, track, 1.f, 100.f, spec, {32, A::RepeatMode::PingPong});
    controller.Advance(0., true);
    events = Drain(controller);
    Require(events.size() == 3 && Count(events, A::EventKind::Completed) == 1 &&
        controller.Read(owner, track)->value == 1.f && controller.Read(owner, track)->velocity == 0.f,
        "reduced motion did not end velocity handoff once");
    controller.SpringTo(owner, track, 1.f, 0.f, spec);
    events = Drain(controller);
    Require(events.size() == 3 && Count(events, A::EventKind::Completed) == 1,
        "resting zero-distance handoff lifecycle");
    controller.RemoveOwner(owner);
    Require(controller.SpringTo(owner, track, 1.f, 1.f, spec) == A::RequestResult::InvalidHandle,
        "stale owner accepted handoff");
}
void SpringHandoffCapacityAndFiniteExtremes() {
    const auto spec = A::MotionSpec::Spring(2.f, 1.f, 1.e-6f);
    A::AnimationController controller({1, 1, 3});
    const auto owner = controller.CreateOwner();
    const auto track = controller.CreateTrack(owner);
    Require(controller.SpringTo(owner, track, 1.f, 2.f, spec) == A::RequestResult::Accepted,
        "minimal handoff reservation");
    const auto original = *controller.Read(owner, track);
    Require(controller.SpringTo(owner, track, 2.f, -2.f, spec) == A::RequestResult::EventCapacityExceeded,
        "handoff bypassed event reservation");
    const auto rejected = *controller.Read(owner, track);
    Require(rejected.generation == original.generation && rejected.value == original.value &&
        rejected.target == original.target && rejected.velocity == original.velocity,
        "capacity rejection changed handoff");
    for (int i = 0; i < 3000 && controller.Read(owner, track)->active; ++i) controller.Advance(.01);
    Require(controller.PendingEventCount() == 3 && controller.Read(owner, track)->value == 1.f,
        "queue fullness stalled accepted handoff");
    const float maximum = std::numeric_limits<float>::max();
    for (const float velocity : {-maximum, maximum}) {
        for (const float target : {-maximum, 0.f, maximum}) {
            A::ScalarChannel channel;
            Require(channel.SpringTo(target, velocity, spec), "finite extreme handoff rejected");
            for (int i = 0; i < 12000 && channel.IsActive(); ++i) {
                channel.Advance(.01);
                Require(std::isfinite(channel.Value()) && std::isfinite(channel.Velocity()),
                    "extreme handoff escaped finite output");
            }
            Require(channel.Value() == target && channel.Velocity() == 0.f && !channel.IsActive(),
                "extreme handoff did not settle");
        }
    }
    A::ScalarChannel scalar(3.f);
    const auto before = scalar.Snapshot();
    Require(!scalar.SpringTo(1.f, std::numeric_limits<float>::infinity()) &&
        !scalar.SpringTo(1.f, 2.f, A::MotionSpec::Tween(.2f)) &&
        scalar.Generation() == before.generation && scalar.Value() == before.value,
        "standalone handoff rejection mutated state");
}
void ControllerScopeIsolation() {
    A::AnimationEvent stale;
    A::OwnerId oldOwner;
    A::TrackId oldTrack;
    {
        A::AnimationController first;
        oldOwner = first.CreateOwner();
        oldTrack = first.CreateTrack(oldOwner);
        first.Retarget(oldOwner, oldTrack, 1.f, A::MotionSpec::Tween(.1f));
        first.Advance(.25);
        stale = Drain(first).back();
        A::AnimationController concurrent;
        const auto owner = concurrent.CreateOwner();
        const auto track = concurrent.CreateTrack(owner);
        Require(owner != oldOwner && track != oldTrack && !concurrent.IsCurrent(stale),
            "concurrent controller adopted stale identity");
        Require(!concurrent.Read(oldOwner, oldTrack) && !concurrent.Read(owner, oldTrack) &&
            !concurrent.Contains(oldOwner), "cross-controller handles accepted");
    }
    A::AnimationController replacement;
    const auto owner = replacement.CreateOwner();
    const auto track = replacement.CreateTrack(owner);
    replacement.Retarget(owner, track, 1.f, A::MotionSpec::Tween(.1f));
    replacement.Advance(.25);
    Require(!replacement.IsCurrent(stale) && owner != oldOwner && track != oldTrack,
        "recreated controller adopted stale event");
    Require(replacement.Cancel(oldOwner, oldTrack) == A::RequestResult::InvalidHandle,
        "recreated controller accepted stale mutation");
}
void MaximumFiniteRepeatReservation() {
    A::AnimationController controller({1, 1, 67});
    const auto owner = controller.CreateOwner();
    const auto track = controller.CreateTrack(owner);
    Require(controller.Retarget(owner, track, 1.f, A::MotionSpec::Tween(0.f),
        {100000, A::RepeatMode::PingPong}) == A::RequestResult::Accepted, "max repeat reservation");
    controller.Advance(1000.);
    const auto events = Drain(controller);
    Require(events.size() == 67 && Count(events, A::EventKind::Repeated) == 32 &&
        Count(events, A::EventKind::Completed) == 1 && !controller.Read(owner, track)->active,
        "max repeat bounded lifecycle");
}
} // namespace

int main() {
    try {
        ScalarTimingAndLegacyRates();
        ExponentialSubUlpProgress();
        FullRangeRelativeSettlement();
        TweenReverseRepeatAndComposition();
        SpringSolutionsAndRetargeting();
        InvalidInputsAndReducedMotion();
        QueuedLifecycleAndReentry();
        ObservedRepeatsReverseAndCancellation();
        CapacityLifetimeAndIsolation();
        MaximumFiniteRepeatReservation();
        ControllerScopeIsolation();
        ExplicitSpringVelocityHandoff();
        SpringHandoffLifecycleRegrabAndGuards();
        SpringHandoffCapacityAndFiniteExtremes();
        std::cout << "Animation scalar, lifecycle, repeat, guard, capacity and owner checks passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Animation core test failed: " << error.what() << '\n';
        return 1;
    }
}
