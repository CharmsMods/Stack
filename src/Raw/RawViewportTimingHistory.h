#pragma once
#include "Raw/RawViewportCalibration.h"
#include <filesystem>
#include <memory>
#include <mutex>

namespace Raw {
// File work runs in TaskSystem. The editor only polls or copies small numeric
// profiles; background tasks never capture the editor's lifetime.
class ViewportTimingHistory {
public:
    ~ViewportTimingHistory() { Reset(); }
    void Open(const std::filesystem::path& directory, const std::string& hardware, int width, int height,
        const std::string& representation = "camera-raw");
    bool MergeLoaded(ViewportTimingBank& bank);
    void Record(const ViewportTimingBank::Keys& keys, const ViewportCalibrationSample& sample,
        std::uint64_t epoch = std::numeric_limits<std::uint64_t>::max());
    std::uint64_t Epoch() const;
    void Save(const ViewportTimingBank& bank);
    void Flush(bool force = false);
    void Reset();
    void ResetHardware();
    std::string Status() const;
    static nlohmann::json Encode(const ViewportTimingBank& bank);
    static ViewportTimingBank Decode(const nlohmann::json& document);
private:
    struct State;
    static void FlushState(const std::shared_ptr<State>& state, bool force);
    static std::mutex& RepositoryMutex();
    static std::map<std::filesystem::path, std::shared_ptr<State>>& Repository();
    std::shared_ptr<State> m_State;
    std::uint64_t m_ObservedRevision = 0;
};
}
