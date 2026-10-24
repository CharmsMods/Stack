// SPDX-License-Identifier: GPL-3.0-only
#include "AnimationController.h"
#include "AnimationInternals.h"
#include <algorithm>
#include <cassert>
#include <atomic>
#include <cmath>
#include <limits>

namespace Stack::Animation {
namespace {
std::uint64_t NextScope() {
    // A nonce, not a registry: no owner state or retained references are global.
    // Saturate rather than wrap, so a controller can never adopt stale handles.
    static std::atomic<std::uint64_t> next{1};
    auto current = next.load(std::memory_order_relaxed);
    while (current != 0) {
        const auto following = current == std::numeric_limits<std::uint64_t>::max() ? 0 : current + 1;
        if (next.compare_exchange_weak(current, following, std::memory_order_relaxed)) return current;
    }
    return 0;
}
} // namespace
AnimationController::AnimationController(ControllerLimits limits) : limits_(limits), scope_(NextScope()) {
    // Bound even accidentally oversized caller configuration. Zero remains useful
    // to disable admission explicitly, instead of silently expanding a budget.
    limits_.maxOwners = std::min<std::size_t>(limits_.maxOwners, 65536);
    limits_.maxTracks = std::min<std::size_t>(limits_.maxTracks, 65536);
    limits_.maxQueuedEvents = std::min<std::size_t>(limits_.maxQueuedEvents, 1048576);
    owners_.reserve(limits_.maxOwners);
    tracks_.reserve(limits_.maxTracks);
    events_.reserve(limits_.maxQueuedEvents);
}
OwnerId AnimationController::CreateOwner() {
    if (scope_ == 0 || owners_.size() >= limits_.maxOwners || nextOwner_ == 0) return {};
    const OwnerId id{nextOwner_++, scope_};
    owners_.push_back(id);
    return id;
}
TrackId AnimationController::CreateTrack(OwnerId owner, float initial) {
    if (!Contains(owner) || !std::isfinite(initial) || tracks_.size() >= limits_.maxTracks ||
        nextTrack_ == 0) return {};
    const TrackId id{nextTrack_++, scope_};
    tracks_.push_back({owner, id, ScalarChannel(initial)});
    return id;
}
bool AnimationController::Contains(OwnerId owner) const {
    return owner && std::find(owners_.begin(), owners_.end(), owner) != owners_.end();
}
AnimationController::Track* AnimationController::Find(OwnerId owner, TrackId track) {
    const auto found = std::find_if(tracks_.begin(), tracks_.end(), [&](const Track& item) {
        return item.owner == owner && item.id == track;
    });
    return found == tracks_.end() ? nullptr : &*found;
}
const AnimationController::Track* AnimationController::Find(OwnerId owner, TrackId track) const {
    const auto found = std::find_if(tracks_.begin(), tracks_.end(), [&](const Track& item) {
        return item.owner == owner && item.id == track;
    });
    return found == tracks_.end() ? nullptr : &*found;
}
std::optional<TrackSnapshot> AnimationController::Read(OwnerId owner, TrackId track) const {
    const auto* found = Find(owner, track);
    return found ? std::optional<TrackSnapshot>(found->channel.Snapshot()) : std::nullopt;
}
bool AnimationController::IsCurrent(const AnimationEvent& event) const {
    const auto* track = Find(event.owner, event.track);
    return track && track->channel.Generation() == event.generation;
}
bool AnimationController::RemoveTrack(OwnerId owner, TrackId track) {
    auto* found = Find(owner, track);
    if (!found) return false;
    reservedEvents_ -= found->channel.ReservedEvents();
    events_.erase(std::remove_if(events_.begin(), events_.end(), [&](const AnimationEvent& event) {
        return event.owner == owner && event.track == track;
    }), events_.end());
    tracks_.erase(std::remove_if(tracks_.begin(), tracks_.end(), [&](const Track& item) {
        return item.owner == owner && item.id == track;
    }), tracks_.end());
    return true;
}
bool AnimationController::RemoveOwner(OwnerId owner) {
    if (!Contains(owner)) return false;
    for (const auto& track : tracks_) {
        if (track.owner == owner) reservedEvents_ -= track.channel.ReservedEvents();
    }
    tracks_.erase(std::remove_if(tracks_.begin(), tracks_.end(), [&](const Track& track) {
        return track.owner == owner;
    }), tracks_.end());
    events_.erase(std::remove_if(events_.begin(), events_.end(), [&](const AnimationEvent& event) {
        return event.owner == owner;
    }), events_.end());
    owners_.erase(std::remove(owners_.begin(), owners_.end(), owner), owners_.end());
    return true;
}
void AnimationController::Append(const Track& track, const Detail::EventBatch& batch) {
    assert(events_.size() + batch.size + reservedEvents_ <= limits_.maxQueuedEvents);
    for (std::size_t i = 0; i < batch.size; ++i) {
        auto event = batch.events[i];
        event.owner = track.owner;
        event.track = track.id;
        events_.push_back(event);
    }
}
RequestResult AnimationController::Commit(Track& track, const ScalarChannel& candidate,
    const Detail::EventBatch& batch) {
    const auto reservation = reservedEvents_ - track.channel.ReservedEvents() + candidate.ReservedEvents();
    if (events_.size() + batch.size + reservation > limits_.maxQueuedEvents)
        return RequestResult::EventCapacityExceeded;
    track.channel = candidate;
    reservedEvents_ = reservation;
    Append(track, batch);
    return RequestResult::Accepted;
}
RequestResult AnimationController::Start(OwnerId owner, TrackId track, float target,
    const MotionSpec& spec, const Playback& playback, bool replay) {
    auto* found = Find(owner, track);
    if (!found) return RequestResult::InvalidHandle;
    if (!std::isfinite(target)) return RequestResult::InvalidValue;
    auto candidate = found->channel;
    Detail::EventBatch batch;
    if (!candidate.RetargetImpl(target, spec, playback, replay, &batch)) return RequestResult::Unchanged;
    return Commit(*found, candidate, batch);
}
RequestResult AnimationController::Retarget(OwnerId owner, TrackId track, float target,
    const MotionSpec& spec, const Playback& playback) {
    return Start(owner, track, target, spec, playback, false);
}
RequestResult AnimationController::Replay(OwnerId owner, TrackId track, float target,
    const MotionSpec& spec, const Playback& playback) {
    return Start(owner, track, target, spec, playback, true);
}
RequestResult AnimationController::SpringTo(OwnerId owner, TrackId track, float target,
    float initialVelocity, const MotionSpec& spec, const Playback& playback) {
    auto* found = Find(owner, track);
    if (!found) return RequestResult::InvalidHandle;
    if (!std::isfinite(target) || !std::isfinite(initialVelocity) || spec.kind != MotionKind::Spring)
        return RequestResult::InvalidValue;
    auto candidate = found->channel;
    Detail::EventBatch batch;
    if (!candidate.SpringToImpl(target, initialVelocity, spec, playback, &batch)) return RequestResult::Unchanged;
    return Commit(*found, candidate, batch);
}
RequestResult AnimationController::Reverse(OwnerId owner, TrackId track) {
    auto* found = Find(owner, track);
    if (!found) return RequestResult::InvalidHandle;
    auto candidate = found->channel;
    Detail::EventBatch batch;
    if (!candidate.ReverseImpl(&batch)) return RequestResult::Unchanged;
    return Commit(*found, candidate, batch);
}
RequestResult AnimationController::Snap(OwnerId owner, TrackId track, float value) {
    auto* found = Find(owner, track);
    if (!found) return RequestResult::InvalidHandle;
    if (!std::isfinite(value)) return RequestResult::InvalidValue;
    if (!found->channel.IsActive() && found->channel.Value() == value && found->channel.Target() == value)
        return RequestResult::Unchanged;
    auto candidate = found->channel;
    Detail::EventBatch batch;
    candidate.SnapImpl(value, &batch);
    return Commit(*found, candidate, batch);
}
RequestResult AnimationController::Cancel(OwnerId owner, TrackId track) {
    auto* found = Find(owner, track);
    if (!found) return RequestResult::InvalidHandle;
    if (!found->channel.IsActive()) return RequestResult::Unchanged;
    auto candidate = found->channel;
    Detail::EventBatch batch;
    candidate.CancelImpl(CancelReason::Requested, &batch);
    return Commit(*found, candidate, batch);
}
RequestResult AnimationController::Finish(OwnerId owner, TrackId track) {
    auto* found = Find(owner, track);
    if (!found) return RequestResult::InvalidHandle;
    if (!found->channel.IsActive()) return RequestResult::Unchanged;
    auto candidate = found->channel;
    Detail::EventBatch batch;
    candidate.FinishImpl(&batch);
    return Commit(*found, candidate, batch);
}
void AnimationController::Advance(double deltaSeconds, bool reducedMotion) {
    for (auto& track : tracks_) {
        if (!track.channel.IsActive()) continue;
        const auto previousReservation = track.channel.ReservedEvents();
        Detail::EventBatch batch;
        track.channel.AdvanceImpl(deltaSeconds, reducedMotion, &batch);
        reservedEvents_ = reservedEvents_ - previousReservation + track.channel.ReservedEvents();
        Append(track, batch);
    }
}
std::size_t AnimationController::DrainEvents(AnimationEvent* output, std::size_t capacity) {
    if (!output || capacity == 0) return 0;
    const auto count = std::min(capacity, events_.size());
    std::copy_n(events_.begin(), count, output);
    events_.erase(events_.begin(), events_.begin() + static_cast<std::ptrdiff_t>(count));
    return count;
}
} // namespace Stack::Animation
