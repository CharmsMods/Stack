#include <thread>
#include <chrono>
#include "Raw/RawViewportPreferences.h"
#include "ThirdParty/json.hpp"
#include <fstream>
#include <map>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace Raw {
namespace {
int ReadInt(const nlohmann::json& doc, const char* key, int fallback, int lo, int hi) {
    const auto it = doc.find(key);
    if (it == doc.end() || !it->is_number()) return fallback;
    const double value = it->get<double>();
    return std::isfinite(value) ? static_cast<int>(std::clamp(value, double(lo), double(hi))) : fallback;
}
bool ReadBool(const nlohmann::json& doc, const char* key, bool fallback) {
    const auto it = doc.find(key);
    return it != doc.end() && it->is_boolean() ? it->get<bool>() : fallback;
}
}
ViewportPreferencesStore::~ViewportPreferencesStore() {
    // Application shutdown discards optional TaskSystem work. Preferences
    // own their small writer so closing immediately after an edit cannot
    // discard the user's last value or the one-time legacy migration.
    if (m_Writer.joinable()) m_Writer.join();
}
bool ViewportPreferencesStore::IsPersisted() const {
    std::lock_guard<std::mutex> lock(m_Mutex);
    return !m_Writing && m_SaveError.empty();
}
std::shared_ptr<ViewportPreferencesStore> ViewportPreferencesStore::Open(const std::filesystem::path& directory) {
    static std::mutex mutex;
    static std::map<std::filesystem::path, std::shared_ptr<ViewportPreferencesStore>> stores;
    std::lock_guard<std::mutex> lock(mutex);
    const auto key = directory.lexically_normal();
    auto& store = stores[key];
    if (!store) {
        store = std::make_shared<ViewportPreferencesStore>();
        store->Load(directory);
    }
    return store;
}
void ViewportPreferencesStore::Load(const std::filesystem::path& directory) {
    m_Path = directory / "RawViewportSettings.json";
    std::error_code error;
    const bool legacy = !std::filesystem::exists(m_Path, error);
    const auto path = legacy ? directory / "RawWorkspaceState.json" : m_Path;
    const auto bytes = std::filesystem::file_size(path, error);
    if (!error && bytes <= 4 * 1024 * 1024) {
        std::ifstream input(path);
        const auto doc = nlohmann::json::parse(input, nullptr, false);
        if (doc.is_object()) {
            m_Value.targetFps = ReadInt(doc, legacy ? "rawViewportTargetFps" : "targetFps", 30, 5, kMaximumViewportTargetFps);
            m_Value.fadeBelowFps = ReadInt(doc, legacy ? "rawViewportFadeBelowFps" : "fadeBelowFps", 30, 5, kMaximumViewportFadeBelowFps);
            m_Value.smoothUpdates = ReadBool(doc, legacy ? "smoothRawViewportUpdates" : "smoothUpdates", true);
            m_Value.minimumDetailPercent = ReadInt(doc, "minimumDetailPercent", 100, 25, 100);
            m_Value.mode = ReadInt(doc, "mode", 0, 0, 1) == 1 ? ViewportInteractionMode::PreserveDetail : ViewportInteractionMode::TargetFps;
            m_Value.backgroundLearning = ReadBool(doc, "backgroundLearning", true);
        }
    }
    // Establish the new record before any workspace save can remove legacy fields.
    if (legacy) Set(m_Value);
}
ViewportPreferences ViewportPreferencesStore::Read() const {
    std::lock_guard<std::mutex> lock(m_Mutex);
    return m_Value;
}
std::uint64_t ViewportPreferencesStore::Revision() const {
    std::lock_guard<std::mutex> lock(m_Mutex);
    return m_Revision;
}
std::string ViewportPreferencesStore::SaveError() const {
    std::lock_guard<std::mutex> lock(m_Mutex);
    return m_SaveError;
}
void ViewportPreferencesStore::Set(ViewportPreferences value) {
    value.targetFps = ClampViewportTargetFps(value.targetFps);
    value.fadeBelowFps = ClampViewportFadeBelowFps(value.fadeBelowFps);
    value.minimumDetailPercent = std::clamp(value.minimumDetailPercent, 25, 100);
    {
        std::lock_guard<std::mutex> lock(m_Mutex);
        m_Value = value;
        ++m_Revision;
        if (m_Path.empty() || m_Writing) return;
        m_Writing=true;
    }
    if (m_Writer.joinable()) m_Writer.join();
    std::lock_guard<std::mutex> lock(m_Mutex);
    try { m_Writer=std::thread([this] { WritePending(); }); }
    catch (const std::exception&) {
        m_Writing=false;
        m_SaveError="Viewport preferences are in memory; storage is unavailable";
    }
}
void ViewportPreferencesStore::WritePending() {
    for (;;) {
        // One short debounce task coalesces a slider gesture; no disk work
        // or filesystem mutex is held by the editor's input thread.
        std::this_thread::sleep_for(std::chrono::milliseconds(250));
        ViewportPreferences value;
        std::uint64_t revision=0;
        { std::lock_guard<std::mutex> lock(m_Mutex); value=m_Value; revision=m_Revision; }
        std::string failure;
        try {
            std::filesystem::create_directories(m_Path.parent_path());
            const nlohmann::json doc {{"version",1}, {"targetFps",value.targetFps},
                {"smoothUpdates",value.smoothUpdates}, {"fadeBelowFps",value.fadeBelowFps},
                {"mode",static_cast<int>(value.mode)}, {"minimumDetailPercent",value.minimumDetailPercent},
                {"backgroundLearning",value.backgroundLearning}};
            auto temporary=m_Path; temporary += ".tmp";
            std::ofstream output(temporary,std::ios::binary | std::ios::trunc);
            output << doc.dump(2); output.close();
            if (!output) throw std::runtime_error("Could not write RAW viewport preferences.");
#ifdef _WIN32
            if (!MoveFileExW(temporary.c_str(),m_Path.c_str(),MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
                throw std::runtime_error("Could not replace RAW viewport preferences.");
#else
            std::filesystem::rename(temporary,m_Path);
#endif
        } catch (const std::exception& error) { failure=error.what(); }
        std::lock_guard<std::mutex> lock(m_Mutex);
        m_SaveError=std::move(failure);
        if (revision==m_Revision) { m_Writing=false; return; }
    }
}
}
