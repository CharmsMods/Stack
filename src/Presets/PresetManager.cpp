#include "PresetManager.h"

#include "App/AppPaths.h"
#include "App/AppVersion.h"
#include "Editor/NodeGraph/EditorNodeGraphSerializer.h"
#include "Renderer/GLHelpers.h"

#include <algorithm>
#include <chrono>
#include <cctype>
#include <ctime>
#include <filesystem>

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

std::uintmax_t PresetManager::BuildPresetSignature() const {
    std::uintmax_t signature = 1469598103934665603ull;
    auto mix = [&](std::uintmax_t value) {
        signature ^= value;
        signature *= 1099511628211ull;
    };

    std::error_code ec;
    if (!std::filesystem::exists(m_PresetsPath, ec)) {
        return signature;
    }

    for (const auto& entry : std::filesystem::directory_iterator(m_PresetsPath, ec)) {
        if (ec || !entry.is_regular_file()) {
            continue;
        }
        if (entry.path().extension() != ".stackpreset") {
            continue;
        }
        mix(static_cast<std::uintmax_t>(entry.file_size(ec)));
        const auto writeTime = std::filesystem::last_write_time(entry.path(), ec);
        if (!ec) {
            mix(static_cast<std::uintmax_t>(writeTime.time_since_epoch().count()));
        }
    }
    return signature;
}

void PresetManager::RefreshPresets() {
    EnsureDirectories();

    const std::uintmax_t signature = BuildPresetSignature();
    if (signature == m_LastPresetSignature && !m_UserPresets.empty()) {
        return;
    }

    std::vector<std::shared_ptr<PresetEntry>> oldPresets;
    {
        std::lock_guard<std::mutex> lock(m_Mutex);
        oldPresets = std::move(m_UserPresets);
        m_UserPresets.clear();
    }

    std::vector<std::shared_ptr<PresetEntry>> loaded;
    std::error_code ec;
    if (std::filesystem::exists(m_PresetsPath, ec)) {
        for (const auto& fileEntry : std::filesystem::directory_iterator(m_PresetsPath, ec)) {
            if (ec || !fileEntry.is_regular_file() || fileEntry.path().extension() != ".stackpreset") {
                continue;
            }

            StackBinaryFormat::NodePresetDocument document;
            StackBinaryFormat::NodePresetLoadOptions options;
            options.includeGraphPayload = false;
            options.includeBoundarySockets = true;
            if (!StackBinaryFormat::ReadNodePresetFile(fileEntry.path(), document, options)) {
                continue;
            }

            auto preset = std::make_shared<PresetEntry>();
            preset->id = document.metadata.id;
            preset->displayName = document.metadata.displayName;
            preset->timestamp = document.metadata.timestamp;
            preset->savedWithVersion = document.metadata.savedWithVersion;
            preset->fileName = fileEntry.path().filename().string();
            preset->nodeCount = document.metadata.nodeCount;
            preset->inputCount = document.metadata.inputCount;
            preset->outputCount = document.metadata.outputCount;
            preset->thumbnailBytes = std::move(document.thumbnailBytes);
            preset->thumbnailWidth = 0;
            preset->thumbnailHeight = 0;
            loaded.push_back(std::move(preset));
        }
    }

    std::sort(loaded.begin(), loaded.end(), [](const std::shared_ptr<PresetEntry>& a, const std::shared_ptr<PresetEntry>& b) {
        return a && b ? a->timestamp > b->timestamp : static_cast<bool>(a);
    });

    {
        std::lock_guard<std::mutex> lock(m_Mutex);
        m_UserPresets = std::move(loaded);
        m_LastPresetSignature = signature;
    }

    for (auto& preset : oldPresets) {
        ReleaseTexture(preset);
    }
}

void PresetManager::UploadPresetTextures(int budget) {
    (void)budget;
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
    const std::filesystem::path path = m_PresetsPath / entry.fileName;
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
    const std::filesystem::path path = m_PresetsPath / entry.fileName;
    if (!StackBinaryFormat::WriteNodePresetFile(path, document)) {
        if (outError) *outError = "Stack could not write the preset file.";
        return false;
    }
    m_LastPresetSignature = 0;
    RefreshPresets();
    return true;
}

bool PresetManager::LoadPresetPayload(const PresetEntry& entry, StackBinaryFormat::json& outGraphPayload, std::string* outError) const {
    StackBinaryFormat::NodePresetDocument document;
    if (!ReadUserPresetDocument(entry, document, outError)) {
        return false;
    }
    if (!document.graphPayload.is_object()) {
        if (outError) *outError = "Preset does not contain graph data.";
        return false;
    }
    outGraphPayload = document.graphPayload;
    return true;
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
    const std::filesystem::path path = m_PresetsPath / entry.fileName;
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
    document.thumbnailBytes = thumbnailBytes;
    document.boundarySockets = boundarySockets;
    return WriteUserPresetDocument(entry, document, outError);
}
