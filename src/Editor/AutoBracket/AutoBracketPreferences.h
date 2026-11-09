#pragma once
#include <atomic>
#include <cstdint>
#include <filesystem>
#include <mutex>
#include <string>
#include <thread>

namespace Stack::AutoBracket {
// Application preference shared by Gallery schedulers and automatic workers.
// The owner reads once, coalesces explicit writes, and drains its writer on exit.
class Preferences {
public:
    static Preferences& Get();
    ~Preferences();
    bool Enabled() const { return enabled_.load(std::memory_order_relaxed); }
    void SetEnabled(bool enabled);
    std::string Error() const;
private:
    Preferences();
    void WritePending();
    std::filesystem::path path_;
    std::atomic<bool> enabled_{true};
    mutable std::mutex mutex_;
    std::thread writer_;
    std::uint64_t revision_=0;
    bool writing_=false;
    std::string error_;
};
}
