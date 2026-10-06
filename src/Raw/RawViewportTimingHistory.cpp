#include "Raw/RawViewportTimingHistory.h"
#include "Async/TaskSystem.h"
#include <fstream>
#include <mutex>
#include <sstream>
#include <chrono>
#include <thread>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif
namespace Raw {
namespace {
std::mutex fileMutex;
constexpr std::uintmax_t maximumFileBytes = 4 * 1024 * 1024;
constexpr std::size_t maximumProfiles = 1024;
bool ValidCost(double cost) { return std::isfinite(cost) && cost >= 0 && cost <= 600000; }
}
struct ViewportTimingHistory::State {
    std::mutex mutex;
    std::filesystem::path path;
    std::string context, hardware;
    ViewportTimingBank bank;
    bool ready = false, writing = false;
    std::uint64_t revision = 1, persisted = 0, epoch = 0;
    std::string error;
};
std::mutex& ViewportTimingHistory::RepositoryMutex() { static std::mutex mutex; return mutex; }
std::map<std::filesystem::path,std::shared_ptr<ViewportTimingHistory::State>>& ViewportTimingHistory::Repository() {
    static std::map<std::filesystem::path,std::shared_ptr<State>> repository;
    return repository;
}
nlohmann::json ViewportTimingHistory::Encode(const ViewportTimingBank& bank) {
    nlohmann::json profiles = nlohmann::json::array();
    for (const auto& [key, curve] : bank.m_Profiles) {
        if (profiles.size() >= maximumProfiles) break;
        nlohmann::json points = nlohmann::json::array();
        for (const auto& p : curve) {
            if (points.size() >= 48) break;
            points.push_back({p.edge,p.coldMs,p.warmMs,p.editMs,p.warmSamples,p.editSamples,p.coldSamples});
        }
        profiles.push_back({{"stage", key.first}, {"workload", key.second}, {"points", std::move(points)}});
    }
    return {{"version", kViewportTimingVersion}, {"profiles", std::move(profiles)}};
}

ViewportTimingBank ViewportTimingHistory::Decode(const nlohmann::json& document) {
    ViewportTimingBank bank;
    try {
        if (!document.is_object() || document.at("version") != kViewportTimingVersion) return bank;
        const auto& profiles = document.at("profiles");
        if (!profiles.is_array() || profiles.size() > maximumProfiles) return bank;
        for (const auto& profile : profiles) {
            const auto stage = profile.at("stage").get<std::size_t>();
            const auto workload = profile.at("workload").get<std::size_t>();
            const auto& points = profile.at("points");
            if (stage >= kViewportStageCount || !points.is_array() || points.size() > 48) continue;
            std::vector<ViewportStageSample> curve;
            for (const auto& point : points) {
                if (!point.is_array() || point.size() != 7) continue;
                ViewportStageSample p {point[0].get<int>(), point[1].get<double>(), point[2].get<double>(),
                    point[3].get<double>(), point[4].get<unsigned>(), point[5].get<unsigned>(), point[6].get<unsigned>()};
                if (p.edge <= 0 || p.edge > 131072 || !ValidCost(p.coldMs) || !ValidCost(p.warmMs) ||
                    (p.editMs != -1 && !ValidCost(p.editMs)) || p.warmSamples > 32 || p.editSamples > 32 || p.coldSamples > 32 ||
                    (!p.warmSamples && !p.editSamples && !p.coldSamples) || (!curve.empty() && p.edge <= curve.back().edge)) continue;
                curve.push_back(p);
            }
            if (!curve.empty()) bank.m_Profiles.emplace(std::make_pair(stage, workload), std::move(curve));
        }
    } catch (const nlohmann::json::exception&) { return {}; }
    return bank;
}

void ViewportTimingHistory::Open(const std::filesystem::path& directory, const std::string& hardware,
    int width, int height, const std::string& representation) {
    if (width <= 0 || height <= 0) return;
    const std::string context = hardware + ":" + representation + ":" + std::to_string(width) + "x" +
        std::to_string(height) + ":" + std::to_string(kViewportTimingVersion);
    if (m_State && m_State->context == context && m_State->path.parent_path() == directory) return;
    Reset();
    std::uint64_t hash = 14695981039346656037ull;
    for (unsigned char byte : context) { hash ^= byte; hash *= 1099511628211ull; }
    std::ostringstream name; name << "viewport-" << std::hex << hash << ".json";
    const auto path = (directory / name.str()).lexically_normal();
    std::lock_guard<std::mutex> repositoryLock(RepositoryMutex());
    auto& repository = Repository();
    if (const auto found = repository.find(path); found != repository.end()) {
        m_State = found->second;
        return;
    }
    // Retain shared profiles across source changes, with a bounded idle bank.
    if (repository.size() >= 64) for (auto it = repository.begin(); it != repository.end();) {
        if (it->second.use_count() == 1) it = repository.erase(it); else ++it;
        if (repository.size() < 64) break;
    }
    auto state = std::make_shared<State>();
    state->path = path; state->context = context; state->hardware = hardware;
    m_State = state; repository.emplace(path,state);
    if (!Async::TaskSystem::Get().Submit([state] {
        ViewportTimingBank loaded;
        try {
            std::lock_guard<std::mutex> files(fileMutex);
            std::error_code error;
            const auto size = std::filesystem::file_size(state->path,error);
            if (!error && size <= maximumFileBytes) {
                std::ifstream input(state->path);
                const auto doc = nlohmann::json::parse(input,nullptr,false);
                if (doc.is_object() && doc.value("context",std::string{}) == state->context) loaded = Decode(doc);
            }
            std::lock_guard<std::mutex> lock(state->mutex);
            // Measurements made while reading win over old disk points.
            for (const auto& [key,curve] : loaded.m_Profiles) {
                if (state->bank.m_Profiles.size() >= maximumProfiles && !state->bank.m_Profiles.count(key)) break;
                auto& target = state->bank.m_Profiles[key];
                for (const auto& point : curve) {
                    auto at = std::lower_bound(target.begin(),target.end(),point.edge,
                        [](const auto& a,int edge) { return a.edge < edge; });
                    if ((at == target.end() || at->edge != point.edge) && target.size() < 48) target.insert(at,point);
                }
            }
            state->ready = true; ++state->revision;
            if (state->revision == 2) state->persisted = state->revision;
        } catch (...) {
            std::lock_guard<std::mutex> lock(state->mutex);
            state->ready = true; ++state->revision;
            state->error = "Timing history could not be read; learning remains available";
        }
        FlushState(state, false);
    })) {
        std::lock_guard<std::mutex> lock(state->mutex);
        state->ready = true;
        state->error = "Timing storage is unavailable";
    }
}
bool ViewportTimingHistory::MergeLoaded(ViewportTimingBank& bank) {
    if (!m_State) return false;
    Flush();
    std::lock_guard<std::mutex> lock(m_State->mutex);
    if (!m_State->ready) { if (!m_ObservedRevision) bank.Clear(); return false; }
    if (m_ObservedRevision == m_State->revision) return false;
    bank = m_State->bank;
    m_ObservedRevision = m_State->revision;
    return true;
}
std::uint64_t ViewportTimingHistory::Epoch() const {
    if (!m_State) return 0;
    std::lock_guard<std::mutex> lock(m_State->mutex);
    return m_State->epoch;
}
void ViewportTimingHistory::Record(const ViewportTimingBank::Keys& keys, const ViewportCalibrationSample& sample, std::uint64_t epoch) {
    if (!m_State) return;
    {
        std::lock_guard<std::mutex> lock(m_State->mutex);
        if (epoch!=std::numeric_limits<std::uint64_t>::max() && epoch!=m_State->epoch) return;
        m_State->bank.Record(keys,sample);
        ++m_State->revision;
    }
    Flush();
}
void ViewportTimingHistory::Save(const ViewportTimingBank& bank) {
    // Snapshot import is retained for callers without individual observations.
    // Production editors use Record, so averages are never counted twice.
    if (!m_State) return;
    {
        std::lock_guard<std::mutex> lock(m_State->mutex);
        for (const auto& [key,curve] : bank.m_Profiles) {
            if (m_State->bank.m_Profiles.size() >= maximumProfiles && !m_State->bank.m_Profiles.count(key)) break;
            auto& target = m_State->bank.m_Profiles[key];
            for (const auto& point : curve) {
                auto at = std::lower_bound(target.begin(),target.end(),point.edge,
                    [](const auto& a,int edge) { return a.edge < edge; });
                if (at != target.end() && at->edge == point.edge) {
                    if (point.warmSamples + point.editSamples + point.coldSamples > at->warmSamples + at->editSamples + at->coldSamples) *at = point;
                } else if (target.size() < 48) target.insert(at,point);
            }
        }
        ++m_State->revision;
    }
    Flush(true);
}
void ViewportTimingHistory::Flush(bool force) { FlushState(m_State,force); }
void ViewportTimingHistory::FlushState(const std::shared_ptr<State>& state, bool force) {
    if (!state) return;
    {
        std::lock_guard<std::mutex> lock(state->mutex);
        if (!state->ready || state->writing || state->persisted == state->revision) return;
        state->writing = true;
    }
    // The task owns the repository state until its final revision is saved,
    // even when every editor closes while a write is in progress.
    if (!Async::TaskSystem::Get().Submit([state,force] {
        bool immediate = force;
        for (;;) {
            if (!immediate) std::this_thread::sleep_for(std::chrono::milliseconds(250));
            ViewportTimingBank bank;
            std::uint64_t revision=0,epoch=0;
            {
                std::lock_guard<std::mutex> lock(state->mutex);
                if (state->persisted == state->revision) { state->writing=false; return; }
                bank=state->bank; revision=state->revision; epoch=state->epoch;
            }
            std::string failure;
            try {
                auto document = Encode(bank);
                document["context"] = state->context;
                document["hardware"] = state->hardware;
                const auto contents = document.dump();
                if (contents.size() > maximumFileBytes) throw std::runtime_error("Timing history is full");
                std::lock_guard<std::mutex> files(fileMutex);
                { std::lock_guard<std::mutex> lock(state->mutex);
                  if (epoch != state->epoch) continue; }
                std::filesystem::create_directories(state->path.parent_path());
                auto temporary = state->path; temporary += ".tmp";
                std::ofstream output(temporary,std::ios::binary | std::ios::trunc);
                output.write(contents.data(),contents.size()); output.close();
                if (!output) throw std::runtime_error("Timing history could not be written");
#ifdef _WIN32
                if (!MoveFileExW(temporary.c_str(),state->path.c_str(),MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
                    throw std::runtime_error("Timing history could not be replaced");
#else
                std::filesystem::rename(temporary,state->path);
#endif
                std::vector<std::pair<std::filesystem::file_time_type,std::filesystem::path>> entries;
                for (const auto& entry : std::filesystem::directory_iterator(state->path.parent_path())) {
                    if (entry.is_regular_file() && entry.path().filename().string().rfind("viewport-",0)==0 && entry.path().extension()==".json")
                        entries.emplace_back(entry.last_write_time(),entry.path());
                }
                std::sort(entries.begin(),entries.end());
                for (std::size_t i=0;i+64<entries.size();++i) if (entries[i].second != state->path) {
                    std::error_code error; std::filesystem::remove(entries[i].second,error);
                }
            } catch (const std::exception& error) { failure = error.what(); }
            std::lock_guard<std::mutex> lock(state->mutex);
            if (epoch == state->epoch) {
                state->error = failure;
                if (failure.empty()) state->persisted = revision;
            }
            if (!failure.empty() || state->persisted == state->revision) { state->writing=false; return; }
            immediate=false;
        }
    })) {
        std::lock_guard<std::mutex> lock(state->mutex);
        state->writing = false; state->error = "Timing storage is unavailable";
    }
}
void ViewportTimingHistory::Reset() { Flush(true); m_State.reset(); m_ObservedRevision = 0; }
std::string ViewportTimingHistory::Status() const {
    if (!m_State) return "Waiting for a RAW presentation";
    std::lock_guard<std::mutex> lock(m_State->mutex);
    if (!m_State->ready) return "Loading compatible timing history";
    if (!m_State->error.empty()) return m_State->error;
    if (m_State->writing || m_State->revision != m_State->persisted) return "Learned timings awaiting save";
    return m_State->bank.m_Profiles.empty() ? "No learned timings yet" : "Learned timings saved";
}
void ViewportTimingHistory::ResetHardware() {
    if (!m_State) return;
    const auto hardware = m_State->hardware;
    const auto directory = m_State->path.parent_path();
    std::lock_guard<std::mutex> repositoryLock(RepositoryMutex());
    std::lock_guard<std::mutex> files(fileMutex);
    for (auto& [path,state] : Repository()) if (state->hardware == hardware && path.parent_path() == directory) {
        std::lock_guard<std::mutex> lock(state->mutex);
        state->bank.Clear(); ++state->epoch; ++state->revision;
        state->persisted = state->revision; state->error.clear();
    }
    std::string failure;
    try {
        std::error_code error;
        const std::filesystem::directory_iterator entries(directory,error);
        if (error && error!=std::errc::no_such_file_or_directory) failure="Some timing files could not be reset";
        for (const auto& entry : entries) {
            if (!entry.is_regular_file() || entry.path().extension()!=".json" || entry.path().filename().string().rfind("viewport-",0)!=0) continue;
            if (entry.file_size(error)>maximumFileBytes || error) { error.clear(); continue; }
            std::ifstream input(entry.path());
            auto doc = nlohmann::json::parse(input,nullptr,false); input.close();
            if (doc.is_object() && doc.contains("hardware") && doc["hardware"].is_string() && doc["hardware"].get<std::string>()==hardware) {
                std::filesystem::remove(entry.path(),error);
                if (error) { failure="Some timing files could not be reset"; error.clear(); }
            }
        }
    } catch (const std::exception&) { failure="Some timing files could not be reset"; }
    if (!failure.empty()) for (auto& [path,state] : Repository())
        if (state->hardware==hardware && path.parent_path()==directory) {
            std::lock_guard<std::mutex> lock(state->mutex);
            state->error=failure;
        }
    m_ObservedRevision = 0;
}
}
