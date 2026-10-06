#pragma once
#include "Raw/RawViewportSettings.h"
#include <cstdint>
#include <filesystem>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

namespace Raw {
enum class ViewportInteractionMode { TargetFps, PreserveDetail };
struct ViewportPreferences {
    int targetFps = kDefaultViewportTargetFps;
    bool smoothUpdates = true;
    int fadeBelowFps = kDefaultViewportFadeBelowFps;
    ViewportInteractionMode mode = ViewportInteractionMode::TargetFps;
    int minimumDetailPercent = 100;
    bool backgroundLearning = true;
};

// Shared application preferences. Editors own only an observed revision.
// An instance without a path is useful for embedded/headless editors too.
class ViewportPreferencesStore : public std::enable_shared_from_this<ViewportPreferencesStore> {
public:
    ~ViewportPreferencesStore();
    bool IsPersisted() const;
    static std::shared_ptr<ViewportPreferencesStore> Open(const std::filesystem::path& directory);
    ViewportPreferences Read() const;
    std::uint64_t Revision() const;
    void Set(ViewportPreferences preferences);
    std::string SaveError() const;
private:
    void Load(const std::filesystem::path& directory);
    mutable std::mutex m_Mutex;
    ViewportPreferences m_Value;
    std::uint64_t m_Revision = 1;
    std::filesystem::path m_Path;
    std::string m_SaveError;
    bool m_Writing = false;
    std::thread m_Writer;
    void WritePending();
};
}
