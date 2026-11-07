#pragma once
#include "Protocol.h"

namespace Stack::BurstEngine {
enum class ScratchKind { Worker, Sources };

// Only marked, uniquely named working directories belong to this owner.
// The worker inherits LockHandle(), keeping recovery out until both processes exit.
class ScratchDirectory {
public:
    ScratchDirectory(const std::filesystem::path& cacheRoot, ScratchKind kind);
    ~ScratchDirectory();
    ScratchDirectory(const ScratchDirectory&)=delete;
    ScratchDirectory& operator=(const ScratchDirectory&)=delete;
    const std::filesystem::path& Path() const { return path_; }
    HANDLE LockHandle() const { return lock_.Get(); }
    void Cleanup(); // Run after worker exit, on a background thread.
private:
    std::filesystem::path path_;
    Handle lock_;
    bool cleanupOnRelease_=false;
};

// Call on a maintenance worker, never from drawing. Unknown/unmarked folders
// and reparse points are deliberately left alone. Concurrent sweeps are safe.
std::size_t RecoverAbandonedScratch(const std::filesystem::path& cacheRoot);
}
