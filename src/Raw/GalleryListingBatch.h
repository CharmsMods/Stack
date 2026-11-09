#pragma once
#include "RawWorkspace.h"
#include <chrono>
#include <utility>

namespace Stack::RawWorkspace {
// Worker-owned, append-only path publication. A listing is a browsing hint,
// never evidence that the source's contents or the whole folder were verified.
class GalleryListingBatch {
public:
    using Callback = std::function<void(std::vector<SourceRecord>)>;
    explicit GalleryListingBatch(Callback callback) : callback_(std::move(callback)) {}
    bool Published() const { return !first_; }
    void Append(const SourceRecord& source) {
        if (!callback_) return;
        pending_.push_back(source);
        const auto now = std::chrono::steady_clock::now();
        const auto interval = std::chrono::milliseconds(first_ ? 200 : 500);
        if (pending_.size() >= (first_ ? 32u : 512u) || now-last_ >= interval) Flush();
    }
    void Flush() {
        if (pending_.empty()) return;
        callback_(std::move(pending_));
        pending_.clear();
        first_ = false;
        last_ = std::chrono::steady_clock::now();
    }
private:
    Callback callback_;
    std::vector<SourceRecord> pending_;
    bool first_ = true;
    std::chrono::steady_clock::time_point last_ = std::chrono::steady_clock::now();
};
}
