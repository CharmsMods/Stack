#pragma once

#include "Renderer/GLLoader.h"
#include <functional>

namespace Stack::Renderer {

// Submission-thread owner of one closed batch. The next bounded batch may be
// queued while this one executes. Fence and flush each new batch before
// waiting only for the older one: at most two batches are in flight, without
// emptying the GPU pipeline at every checkpoint. Completion/publication still
// uses the worker's final fence, independently of this scheduling owner.
class CooperativeGpuQueue {
public:
    CooperativeGpuQueue() = default;
    ~CooperativeGpuQueue();
    CooperativeGpuQueue(const CooperativeGpuQueue&) = delete;
    CooperativeGpuQueue& operator=(const CooperativeGpuQueue&) = delete;

    bool Checkpoint(const std::function<bool()>& shouldCancel);

private:
    GLsync m_Batches[2]{};
};

} // namespace Stack::Renderer
