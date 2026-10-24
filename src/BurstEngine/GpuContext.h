#pragma once
#include "Raw/OpenGlTask.h"
#include <memory>

namespace Stack::BurstEngine {
// The standalone batch and IPC host share one GPU owner implementation.
// Construct and retire off the UI thread. Missing GL is reported as a processing failure, never CPU substitution.
class GpuContext {
public:
    GpuContext();
    ~GpuContext();
    GpuContext(const GpuContext&)=delete;
    GpuContext& operator=(const GpuContext&)=delete;
    bool Available() const;
    Raw::OpenGlTaskExecutor Executor();
private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};
}
