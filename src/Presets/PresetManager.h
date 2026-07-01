#pragma once

#include "Persistence/StackBinaryFormat.h"

#include <filesystem>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

struct PresetEntry {
    std::string id;
    std::string displayName;
    std::string timestamp;
    std::string savedWithVersion;
    std::string fileName;
    bool builtIn = false;
    std::uint32_t nodeCount = 0;
    std::uint32_t inputCount = 0;
    std::uint32_t outputCount = 0;
    std::vector<unsigned char> thumbnailBytes;
    unsigned int thumbnailTex = 0;
    int thumbnailWidth = 0;
    int thumbnailHeight = 0;
    bool thumbnailLoadAttempted = false;
    StackBinaryFormat::json graphPayload = StackBinaryFormat::json();
};

class PresetManager {
public:
    static constexpr int kPresetThumbnailWidth = 960;
    static constexpr int kPresetThumbnailHeight = 600;

    static PresetManager& Get();

    void RefreshPresets();
    void UploadPresetTextures(int budget = 4);

    bool SaveUserPreset(
        const std::string& displayName,
        const StackBinaryFormat::json& graphPayload,
        const std::vector<unsigned char>& thumbnailBytes,
        const std::vector<StackBinaryFormat::NodePresetBoundarySocket>& boundarySockets,
        std::uint32_t nodeCount,
        std::string* outError = nullptr);
    bool LoadPresetPayload(const PresetEntry& entry, StackBinaryFormat::json& outGraphPayload, std::string* outError = nullptr) const;
    bool RenameUserPreset(const PresetEntry& entry, const std::string& displayName, std::string* outError = nullptr);
    bool DeleteUserPreset(const PresetEntry& entry, std::string* outError = nullptr);
    bool OverwriteUserPreset(
        const PresetEntry& entry,
        const StackBinaryFormat::json& graphPayload,
        const std::vector<unsigned char>& thumbnailBytes,
        const std::vector<StackBinaryFormat::NodePresetBoundarySocket>& boundarySockets,
        std::uint32_t nodeCount,
        std::string* outError = nullptr);

    const std::vector<std::shared_ptr<PresetEntry>>& GetBuiltInPresets() const { return m_BuiltInPresets; }
    const std::vector<std::shared_ptr<PresetEntry>>& GetUserPresets() const { return m_UserPresets; }

private:
    PresetManager();
    ~PresetManager();

    void EnsureDirectories();
    void BuildBuiltInPresets();
    void ReleaseTexture(std::shared_ptr<PresetEntry> entry);
    bool ReadUserPresetDocument(const PresetEntry& entry, StackBinaryFormat::NodePresetDocument& document, std::string* outError = nullptr) const;
    bool WriteUserPresetDocument(const PresetEntry& entry, const StackBinaryFormat::NodePresetDocument& document, std::string* outError = nullptr);
    std::uintmax_t BuildPresetSignature() const;

    std::filesystem::path m_PresetsPath;
    std::vector<std::shared_ptr<PresetEntry>> m_BuiltInPresets;
    std::vector<std::shared_ptr<PresetEntry>> m_UserPresets;
    std::uintmax_t m_LastPresetSignature = 0;
    mutable std::mutex m_Mutex;
};
