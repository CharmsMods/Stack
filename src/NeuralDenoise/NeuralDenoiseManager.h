#pragma once

#include "NeuralDenoiseTypes.h"
#include "OnnxDenoiseBackend.h"

#include <filesystem>
#include <string>
#include <vector>

namespace NeuralDenoise {

class NeuralDenoiseManager {
public:
    static NeuralDenoiseManager& Instance();

    void EnsureInitialized();
    void Refresh();
    bool IsInitialized() const { return m_Initialized; }
    bool IsManifestLoaded() const { return m_ManifestLoaded; }
    bool IsDenoiseFolderPresent() const { return m_DenoiseFolderPresent; }

    const std::filesystem::path& RootDirectory() const { return m_RootDirectory; }
    const std::filesystem::path& ManifestPath() const { return m_ManifestPath; }
    const std::vector<NeuralDenoiseModelInfo>& Models() const { return m_Models; }
    const std::vector<std::string>& Warnings() const { return m_Warnings; }

    const NeuralDenoiseModelInfo* FindModel(const std::string& id) const;
    const NeuralDenoiseModelInfo* FirstModelOfType(ModelType type) const;
    std::vector<const NeuralDenoiseModelInfo*> ModelsOfType(ModelType type) const;

    ModelAvailability GetAvailability(const NeuralDenoiseModelInfo* model) const;
    std::string OverallStatus() const;
    const OnnxDenoiseBackend& OnnxBackend() const { return m_OnnxBackend; }
    OnnxDenoiseBackend& OnnxBackend() { return m_OnnxBackend; }

private:
    NeuralDenoiseManager();

    std::filesystem::path ResolveDenoiseRoot() const;
    void LoadManifest();

    std::filesystem::path m_RootDirectory;
    std::filesystem::path m_ManifestPath;
    bool m_Initialized = false;
    bool m_DenoiseFolderPresent = false;
    bool m_ManifestLoaded = false;
    int m_ManifestVersion = 0;
    std::vector<NeuralDenoiseModelInfo> m_Models;
    std::vector<std::string> m_Warnings;
    OnnxDenoiseBackend m_OnnxBackend;
};

} // namespace NeuralDenoise
