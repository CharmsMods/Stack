// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include "AnimationTypes.h"
#include <array>
#include <cassert>
#include <cstddef>

namespace Stack::Animation::Detail {
// At most 33 finite traversals: 33 arrivals + 32 repeats + one completion.
// Mutation batches additionally need at most cancellation and start records.
struct EventBatch {
    std::array<AnimationEvent, 72> events{};
    std::size_t size = 0;
    void Push(const AnimationEvent& event) {
        assert(size < events.size());
        if (size < events.size()) events[size++] = event;
    }
};
} // namespace Stack::Animation::Detail
