#include "PresetManager.h"

#include "Async/TaskSystem.h"
#include "App/AppPaths.h"
#include "App/AppVersion.h"
#include "Editor/NodeGraph/EditorNodeGraphSerializer.h"
#include "Renderer/GLHelpers.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cctype>
#include <ctime>
#include <filesystem>
#include <utility>

namespace {

std::string TrimWhitespace(const std::string& value) {
    const auto begin = std::find_if_not(value.begin(), value.end(), [](unsigned char ch) { return std::isspace(ch); });
    const auto end = std::find_if_not(value.rbegin(), value.rend(), [](unsigned char ch) { return std::isspace(ch); }).base();
    return begin < end ? std::string(begin, end) : std::string();
}

std::string BuildTimestampString() {
    std::time_t now = std::time(nullptr);
    std::tm localTime {};
#if defined(_WIN32)
    localtime_s(&localTime, &now);
#else
    localtime_r(&now, &localTime);
#endif
    char buffer[64];
    if (std::strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S", &localTime) == 0) {
        return {};
    }
    return buffer;
}

std::string BuildPresetId() {
    const auto now = std::chrono::system_clock::now();
    const auto millis = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count();
    return "preset-" + std::to_string(millis);
}

std::string SanitizeFileStem(const std::string& name) {
    std::string stem;
    for (unsigned char ch : name) {
        if (std::isalnum(ch) || ch == '-' || ch == '_') {
            stem.push_back(static_cast<char>(ch));
        } else if (std::isspace(ch)) {
            stem.push_back('_');
        }
    }
    while (!stem.empty() && stem.back() == '_') {
        stem.pop_back();
    }
    return stem.empty() ? "preset" : stem;
}

void ScrubPresetImageNodePayload(StackBinaryFormat::json& nodeValue) {
    if (!nodeValue.is_object()) {
        return;
    }

    const std::string kind = nodeValue.value("kind", std::string());
    if (kind == "Image") {
        nodeValue["sourcePath"] = "";
        nodeValue["width"] = 0;
        nodeValue["height"] = 0;
        nodeValue["channels"] = 0;
        nodeValue["originalChannels"] = 0;
        nodeValue.erase("pngBytes");
        return;
    }

    if (kind == "RawSource") {
        nodeValue["sourcePath"] = "";
        if (nodeValue.contains("rawMetadata") && nodeValue["rawMetadata"].is_object()) {
            nodeValue["rawMetadata"]["sourcePath"] = "";
        }
    }
}

bool SanitizePresetGraphPayload(StackBinaryFormat::json& graphPayload) {
    if (!graphPayload.is_object() || !graphPayload.contains("nodeGraph") || !graphPayload["nodeGraph"].is_object()) {
        return false;
    }

    StackBinaryFormat::json& graphJson = graphPayload["nodeGraph"];
    if (!graphJson.contains("nodes") || !graphJson["nodes"].is_array()) {
        return false;
    }

    bool changed = false;
    for (auto& nodeValue : graphJson["nodes"]) {
        const StackBinaryFormat::json originalNodeValue = nodeValue;
        ScrubPresetImageNodePayload(nodeValue);
        if (!changed && nodeValue != originalNodeValue) {
            changed = true;
        }
    }
    return changed;
}

} // namespace

PresetManager& PresetManager::Get() {
    static PresetManager instance;
    return instance;
}

PresetManager::PresetManager() {
    m_PresetsPath = AppPaths::GetPresetsDirectory();
    EnsureDirectories();
    RefreshPresets();
}

PresetManager::~PresetManager() {
    CancelPresetRefresh();
    for (auto& preset : m_UserPresets) {
        ReleaseTexture(preset);
    }
    for (auto& preset : m_BuiltInPresets) {
        ReleaseTexture(preset);
    }
}

void PresetManager::EnsureDirectories() {
    std::error_code ec;
    std::filesystem::create_directories(m_PresetsPath, ec);
}

void PresetManager::BuildBuiltInPresets() {
    m_BuiltInPresets.clear();
}

void PresetManager::ReleaseTexture(std::shared_ptr<PresetEntry> entry) {
    if (entry && entry->thumbnailTex != 0) {
        unsigned int texture = entry->thumbnailTex;
        glDeleteTextures(1, &texture);
        entry->thumbnailTex = 0;
    }
}

namespace {

struct PresetCatalogScan {
    bool success = false;
    bool changed = false;
    std::uintmax_t signature = 0;
    std::vector<std::shared_ptr<PresetEntry>> entries;
};

bool PresetScanCanceled(const std::atomic<bool>* canceled) {
    return canceled && canceled->load(std::memory_order_relaxed);
}

bool BuildPresetSignature(const std::filesystem::path& path,
    std::uintmax_t& signature, const std::atomic<bool>* canceled) {
    signature = 1469598103934665603ull;
    auto mix = [&](std::uintmax_t value) {
        signature ^= value;
        signature *= 1099511628211ull;
    };

    std::error_code ec;
    for (std::filesystem::directory_iterator it(path, ec), end;
         !ec && it != end; it.increment(ec)) {
        if (PresetScanCanceled(canceled)) return false;
        const auto& entry = *it;
        std::error_code entryError;
        const bool regular = entry.is_regular_file(entryError);
        if (entryError) return false;
        if (!regular) continue;
        if (entry.path().extension() != ".stackpreset") {
            continue;
        }
        for (unsigned char ch : entry.path().filename().u8string()) mix(ch);
        const auto size = entry.file_size(entryError);
        if (entryError) return false;
        mix(static_cast<std::uintmax_t>(size));
        const auto writeTime = entry.last_write_time(entryError);
        if (entryError) return false;
        mix(static_cast<std::uintmax_t>(writeTime.time_since_epoch().count()));
    }
    return !ec && !PresetScanCanceled(canceled);
}

PresetCatalogScan ScanPresetCatalog(const std::filesystem::path& path,
    bool hasSignature, std::uintmax_t acceptedSignature,
    const std::atomic<bool>* canceled = nullptr) {
    PresetCatalogScan result;
    if (PresetScanCanceled(canceled)) return result;
    std::error_code ec;
    std::filesystem::create_directories(path, ec);
    if (ec || !BuildPresetSignature(path, result.signature, canceled)) return result;
    if (hasSignature && result.signature == acceptedSignature) {
        result.success = true;
        return result;
    }
    {
        for (std::filesystem::directory_iterator it(path, ec), end;
             !ec && it != end; it.increment(ec)) {
            if (PresetScanCanceled(canceled)) return result;
            const auto& fileEntry = *it;
            std::error_code entryError;
            const bool regular = fileEntry.is_regular_file(entryError);
            if (entryError) return result;
            if (!regular || fileEntry.path().extension() != ".stackpreset") {
                continue;
            }

            StackBinaryFormat::NodePresetDocument document;
            StackBinaryFormat::NodePresetLoadOptions options;
            options.includeGraphPayload = false;
            options.includeBoundarySockets = true;
            if (!StackBinaryFormat::ReadNodePresetFile(fileEntry.path(), document, options)) {
                // The reader cannot distinguish malformed content from a
                // transient individual-file failure. Preserve skip-unreadable
                // semantics rather than let one bad file block the catalog.
                continue;
            }

            auto preset = std::make_shared<PresetEntry>();
            preset->id = document.metadata.id;
            preset->displayName = document.metadata.displayName;
            preset->timestamp = document.metadata.timestamp;
            preset->savedWithVersion = document.metadata.savedWithVersion;
            preset->fileName = fileEntry.path().filename().u8string();
            preset->nodeCount = document.metadata.nodeCount;
            preset->inputCount = document.metadata.inputCount;
            preset->outputCount = document.metadata.outputCount;
            preset->thumbnailBytes = std::move(document.thumbnailBytes);
            preset->thumbnailWidth = 0;
            preset->thumbnailHeight = 0;
            result.entries.push_back(std::move(preset));
        }
    }
    if (ec || PresetScanCanceled(canceled)) return result;
    std::sort(result.entries.begin(), result.entries.end(), [](const std::shared_ptr<PresetEntry>& a, const std::shared_ptr<PresetEntry>& b) {
        return a && b ? a->timestamp > b->timestamp : static_cast<bool>(a);
    });
    result.changed = result.success = true;
    return result;
}

} // namespace

struct PresetManager::RefreshRequest {
    std::atomic<bool> ready { false };
    std::atomic<bool> canceled { false };
    PresetCatalogScan result;
};

void PresetManager::CancelPresetRefresh() {
    if (m_RefreshRequest) m_RefreshRequest->canceled.store(true, std::memory_order_relaxed);
    m_RefreshRequest.reset();
}

void PresetManager::RefreshPresets() {
    // An explicit save/rename/delete must supersede any older background scan.
    CancelPresetRefresh();
    auto result = ScanPresetCatalog(m_PresetsPath, m_HasPresetSignature, m_LastPresetSignature);
    if (result.success && result.changed)
        ApplyPresetCatalog(std::move(result.entries), result.signature);
    m_NextPresetRefresh = std::chrono::steady_clock::now() + std::chrono::seconds(1);
}

void PresetManager::RequestPresetRefresh() {
    const auto now = std::chrono::steady_clock::now();
    if (m_RefreshRequest) {
        if (!m_RefreshRequest->ready.load(std::memory_order_acquire)) return;
        auto request = std::move(m_RefreshRequest);
        m_NextPresetRefresh = now + std::chrono::seconds(1);
        if (request->result.success && request->result.changed)
            ApplyPresetCatalog(std::move(request->result.entries), request->result.signature);
        // Publish a completion before considering another scan, even if slow.
        return;
    }
    if (now < m_NextPresetRefresh) return;
    m_NextPresetRefresh = now + std::chrono::seconds(1);
    try {
        auto request = std::make_shared<RefreshRequest>();
        const auto path = m_PresetsPath;
        const auto signature = m_LastPresetSignature;
        const bool hasSignature = m_HasPresetSignature;
        m_RefreshRequest = request;
        Async::ActivityMetadata activity;
        activity.label = "Refreshing presets";
        activity.maintenance = true;
        const bool submitted = Async::TaskSystem::Get().Submit(std::move(activity),
            [request, path, signature, hasSignature]() {
                try {
                    request->result = ScanPresetCatalog(path, hasSignature, signature, &request->canceled);
                } catch (...) {
                    request->result.success = false;
                }
                request->ready.store(true, std::memory_order_release);
            });
        if (!submitted) CancelPresetRefresh();
    } catch (...) {
        CancelPresetRefresh();
    }
}

void PresetManager::ApplyPresetCatalog(
    std::vector<std::shared_ptr<PresetEntry>> loaded, std::uintmax_t signature) {
    std::vector<std::shared_ptr<PresetEntry>> oldPresets;

    {
        std::lock_guard<std::mutex> lock(m_Mutex);
        const auto catalogRevision = ++m_PreviewDataState->catalogRevision;
        for (auto& preset : loaded) {
            preset->previewContentRevision = catalogRevision;
        }
        oldPresets = std::move(m_UserPresets);
        m_UserPresets = std::move(loaded);
        m_LastPresetSignature = signature;
        m_HasPresetSignature = true;
        ++m_PreviewDataState->revision;
    }

    for (const auto& preset : m_UserPresets) {
        RequestBackgroundGraphPayloadPreload(preset);
    }

    for (auto& preset : oldPresets) {
        ReleaseTexture(preset);
    }
}

void PresetManager::UploadPresetTextures(int budget) {
    (void)budget;
}

void PresetManager::RequestBackgroundGraphPayloadPreload(const std::shared_ptr<PresetEntry>& entry) {
    if (!entry || entry->builtIn || entry->fileName.empty() || entry->graphPayloadLoaded || entry->graphPayloadLoadRequested) {
        return;
    }

    entry->graphPayloadLoadRequested = true;
    entry->graphPayloadLoadError.clear();

    const std::filesystem::path path = m_PresetsPath / std::filesystem::u8path(entry->fileName);
    const std::weak_ptr<PresetEntry> weakEntry = entry;
    const std::weak_ptr<PreviewDataState> weakPreviewState = m_PreviewDataState;
    const auto catalogRevision = m_PreviewDataState->catalogRevision;
    const bool queued = Async::TaskSystem::Get().Submit("Loading presets", [path, weakEntry, weakPreviewState, catalogRevision]() {
        StackBinaryFormat::json graphPayload = StackBinaryFormat::json::object();
        std::string error;
        bool success = false;

        StackBinaryFormat::NodePresetDocument document;
        StackBinaryFormat::NodePresetLoadOptions options;
        options.includeThumbnail = false;
        options.includeGraphPayload = true;
        options.includeBoundarySockets = true;
        try {
            if (StackBinaryFormat::ReadNodePresetFile(path, document, options) && document.graphPayload.is_object()) {
                graphPayload = std::move(document.graphPayload);
                // Preloading must not rewrite a partial document or race an
                // explicit rename/overwrite. Apply source scrubbing in memory.
                SanitizePresetGraphPayload(graphPayload);
                success = true;
            } else {
                error = "Stack could not read the preset graph.";
            }
        } catch (...) {
            error = "Stack could not read the preset graph.";
        }

        Async::TaskSystem::Get().PostToMain([weakEntry, weakPreviewState, catalogRevision,
            graphPayload = std::move(graphPayload), error = std::move(error), success]() mutable {
            if (const std::shared_ptr<PresetEntry> entry = weakEntry.lock()) {
                entry->graphPayloadLoadRequested = false;
                if (success) {
                    entry->graphPayload = std::move(graphPayload);
                    entry->graphPayloadLoaded = true;
                    entry->graphPayloadLoadError.clear();
                } else {
                    entry->graphPayload = StackBinaryFormat::json();
                    entry->graphPayloadLoaded = false;
                    entry->graphPayloadLoadError = error;
                }
                // Complete retained old entries as before, but only current
                // catalog payloads can wake another preview warmup pass.
                if (const auto previewState = weakPreviewState.lock()) {
                    if (previewState->catalogRevision == catalogRevision)
                        ++previewState->revision;
                }
            }
        });
    });
    if (!queued) {
        entry->graphPayloadLoadRequested = false;
        entry->graphPayloadLoadError = "Stack could not schedule the preset graph load.";
        ++m_PreviewDataState->revision;
    }
}

bool PresetManager::SaveUserPreset(
    const std::string& displayName,
    const StackBinaryFormat::json& graphPayload,
    const std::vector<unsigned char>& thumbnailBytes,
    const std::vector<StackBinaryFormat::NodePresetBoundarySocket>& boundarySockets,
    std::uint32_t nodeCount,
    std::string* outError) {
    EnsureDirectories();

    const std::string trimmedName = TrimWhitespace(displayName).empty() ? "Untitled Preset" : TrimWhitespace(displayName);
    const std::string id = BuildPresetId();
    const std::string fileName = SanitizeFileStem(trimmedName) + "_" + id + ".stackpreset";

    StackBinaryFormat::NodePresetDocument document;
    document.metadata.id = id;
    document.metadata.displayName = trimmedName;
    document.metadata.timestamp = BuildTimestampString();
    document.metadata.savedWithVersion = AppVersion::kVersionString;
    document.metadata.nodeCount = nodeCount;
    document.metadata.inputCount = static_cast<std::uint32_t>(std::count_if(
        boundarySockets.begin(),
        boundarySockets.end(),
        [](const StackBinaryFormat::NodePresetBoundarySocket& socket) { return socket.direction == "input"; }));
    document.metadata.outputCount = static_cast<std::uint32_t>(boundarySockets.size()) - document.metadata.inputCount;
    document.thumbnailBytes = thumbnailBytes;
    document.graphPayload = graphPayload;
    SanitizePresetGraphPayload(document.graphPayload);
    document.boundarySockets = boundarySockets;

    const std::filesystem::path path = m_PresetsPath / fileName;
    if (!StackBinaryFormat::WriteNodePresetFile(path, document)) {
        if (outError) {
            *outError = "Stack could not write the preset file.";
        }
        return false;
    }

    m_LastPresetSignature = 0;
    RefreshPresets();
    return true;
}

bool PresetManager::TryGetPreloadedPresetPayload(const PresetEntry& entry, StackBinaryFormat::json& outGraphPayload) const {
    if (!entry.graphPayloadLoaded || !entry.graphPayload.is_object()) {
        return false;
    }

    outGraphPayload = entry.graphPayload;
    return true;
}

bool PresetManager::ReadUserPresetDocument(const PresetEntry& entry, StackBinaryFormat::NodePresetDocument& document, std::string* outError) const {
    if (entry.builtIn) {
        document.metadata.id = entry.id;
        document.metadata.displayName = entry.displayName;
        document.metadata.timestamp = entry.timestamp;
        document.metadata.savedWithVersion = entry.savedWithVersion;
        document.metadata.nodeCount = entry.nodeCount;
        document.metadata.inputCount = entry.inputCount;
        document.metadata.outputCount = entry.outputCount;
        document.thumbnailBytes = entry.thumbnailBytes;
        document.graphPayload = entry.graphPayload;
        return true;
    }
    if (entry.fileName.empty()) {
        if (outError) *outError = "Preset file name is missing.";
        return false;
    }
    const std::filesystem::path path = m_PresetsPath / std::filesystem::u8path(entry.fileName);
    if (!StackBinaryFormat::ReadNodePresetFile(path, document)) {
        if (outError) *outError = "Stack could not read the preset file.";
        return false;
    }
    return true;
}

bool PresetManager::WriteUserPresetDocument(const PresetEntry& entry, const StackBinaryFormat::NodePresetDocument& document, std::string* outError) {
    if (entry.builtIn || entry.fileName.empty()) {
        if (outError) *outError = "Built-in presets cannot be modified.";
        return false;
    }
    const std::filesystem::path path = m_PresetsPath / std::filesystem::u8path(entry.fileName);
    if (!StackBinaryFormat::WriteNodePresetFile(path, document)) {
        if (outError) *outError = "Stack could not write the preset file.";
        return false;
    }
    m_LastPresetSignature = 0;
    RefreshPresets();
    return true;
}

bool PresetManager::LoadPresetPayload(const PresetEntry& entry, StackBinaryFormat::json& outGraphPayload, std::string* outError) const try {
    if (TryGetPreloadedPresetPayload(entry, outGraphPayload)) {
        return true;
    }

    StackBinaryFormat::NodePresetDocument document;
    if (!ReadUserPresetDocument(entry, document, outError)) {
        return false;
    }
    if (!document.graphPayload.is_object()) {
        if (outError) *outError = "Preset does not contain graph data.";
        return false;
    }
    SanitizePresetGraphPayload(document.graphPayload);
    outGraphPayload = document.graphPayload;
    return true;
} catch (const std::exception&) {
    if (outError) *outError = "Stack could not read the preset graph.";
    return false;
}

bool PresetManager::RenameUserPreset(const PresetEntry& entry, const std::string& displayName, std::string* outError) {
    StackBinaryFormat::NodePresetDocument document;
    if (!ReadUserPresetDocument(entry, document, outError)) {
        return false;
    }
    document.metadata.displayName = TrimWhitespace(displayName).empty() ? "Untitled Preset" : TrimWhitespace(displayName);
    return WriteUserPresetDocument(entry, document, outError);
}

bool PresetManager::DeleteUserPreset(const PresetEntry& entry, std::string* outError) {
    if (entry.builtIn || entry.fileName.empty()) {
        if (outError) *outError = "Built-in presets cannot be deleted.";
        return false;
    }
    std::error_code ec;
    const std::filesystem::path path = m_PresetsPath / std::filesystem::u8path(entry.fileName);
    if (!std::filesystem::remove(path, ec) || ec) {
        if (outError) *outError = "Stack could not delete the preset file.";
        return false;
    }
    m_LastPresetSignature = 0;
    RefreshPresets();
    return true;
}

bool PresetManager::OverwriteUserPreset(
    const PresetEntry& entry,
    const StackBinaryFormat::json& graphPayload,
    const std::vector<unsigned char>& thumbnailBytes,
    const std::vector<StackBinaryFormat::NodePresetBoundarySocket>& boundarySockets,
    std::uint32_t nodeCount,
    std::string* outError) {
    StackBinaryFormat::NodePresetDocument document;
    if (!ReadUserPresetDocument(entry, document, outError)) {
        return false;
    }
    document.metadata.timestamp = BuildTimestampString();
    document.metadata.savedWithVersion = AppVersion::kVersionString;
    document.metadata.nodeCount = nodeCount;
    document.metadata.inputCount = static_cast<std::uint32_t>(std::count_if(
        boundarySockets.begin(),
        boundarySockets.end(),
        [](const StackBinaryFormat::NodePresetBoundarySocket& socket) { return socket.direction == "input"; }));
    document.metadata.outputCount = static_cast<std::uint32_t>(boundarySockets.size()) - document.metadata.inputCount;
    document.graphPayload = graphPayload;
    SanitizePresetGraphPayload(document.graphPayload);
    document.thumbnailBytes = thumbnailBytes;
    document.boundarySockets = boundarySockets;
    return WriteUserPresetDocument(entry, document, outError);
}
