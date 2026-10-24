// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include "ScalarChannel.h"
#include <cstddef>
#include <optional>
#include <vector>

namespace Stack::Animation {

struct ControllerLimits {
    std::size_t maxOwners = 64;
    std::size_t maxTracks = 1024;
    std::size_t maxQueuedEvents = 4096;
};
enum class RequestResult { Accepted, Unchanged, InvalidHandle, InvalidValue, EventCapacityExceeded };

// Optional, presentation-owner-local observed timelines. Not a global registry.
// Single-threaded: its presentation owner mutates/advances once per frame, then
// dispatches drained events. The scope nonce does not make this class thread-safe.
// Construction reserves storage; accepted transitions never allocate or stall in
// Advance. Full event capacity rejects a NEW request before changing its value.
// Every accepted run reserves its complete finite lifecycle, so delayed drains
// cannot drop its events or prevent its visual completion. Standalone channels
// are preferable for decorative controls that do not consume lifecycle events.
class AnimationController {
public:
    explicit AnimationController(ControllerLimits limits = {});
    AnimationController(const AnimationController&) = delete;
    AnimationController& operator=(const AnimationController&) = delete;
    AnimationController(AnimationController&&) = delete;
    AnimationController& operator=(AnimationController&&) = delete;

    OwnerId CreateOwner();
    TrackId CreateTrack(OwnerId owner, float initial = 0.f);
    // Lifetime invalidation is silent: remove tracks and their queued events,
    // rather than allowing teardown to trigger application behavior.
    bool RemoveOwner(OwnerId owner);
    bool RemoveTrack(OwnerId owner, TrackId track);
    bool Contains(OwnerId owner) const;
    std::optional<TrackSnapshot> Read(OwnerId owner, TrackId track) const;
    bool IsCurrent(const AnimationEvent& event) const;

    RequestResult Retarget(OwnerId owner, TrackId track, float target,
        const MotionSpec& spec, const Playback& playback = {});
    RequestResult Replay(OwnerId owner, TrackId track, float target,
        const MotionSpec& spec, const Playback& playback = {});
    // Explicit, opt-in signed-velocity handoff. InvalidValue includes non-Spring
    // specs. Queue rejection preserves the old run and supplied input unchanged.
    RequestResult SpringTo(OwnerId owner, TrackId track, float target, float initialVelocity,
        const MotionSpec& spec = MotionSpec::Spring(), const Playback& playback = {});
    RequestResult Reverse(OwnerId owner, TrackId track);
    RequestResult Snap(OwnerId owner, TrackId track, float value);
    RequestResult Cancel(OwnerId owner, TrackId track);
    RequestResult Finish(OwnerId owner, TrackId track);
    void Advance(double deltaSeconds, bool reducedMotion = false);

    // FIFO copy-and-remove, no callbacks or allocations. Drain before dispatching
    // outside drawing. Events generated while dispatching await the NEXT drain.
    std::size_t DrainEvents(AnimationEvent* output, std::size_t capacity);
    std::size_t PendingEventCount() const { return events_.size(); }
    std::size_t OwnerCount() const { return owners_.size(); }
    std::size_t TrackCount() const { return tracks_.size(); }

private:
    struct Track {
        OwnerId owner;
        TrackId id;
        ScalarChannel channel;
    };
    Track* Find(OwnerId owner, TrackId track);
    const Track* Find(OwnerId owner, TrackId track) const;
    RequestResult Start(OwnerId owner, TrackId track, float target,
        const MotionSpec& spec, const Playback& playback, bool replay);
    RequestResult Commit(Track& track, const ScalarChannel& candidate,
        const Detail::EventBatch& batch);
    void Append(const Track& track, const Detail::EventBatch& batch);

    ControllerLimits limits_;
    const std::uint64_t scope_;
    std::vector<OwnerId> owners_;
    std::vector<Track> tracks_;
    std::vector<AnimationEvent> events_;
    std::size_t reservedEvents_ = 0;
    std::uint64_t nextOwner_ = 1;
    std::uint64_t nextTrack_ = 1;
};

} // namespace Stack::Animation
