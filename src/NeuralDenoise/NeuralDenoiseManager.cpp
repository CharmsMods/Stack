#include "NeuralDenoiseManager.h"

#include <cstdlib>
#include <fstream>
#include <iostream>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

namespace NeuralDenoise {
namespace {

bool DirectoryExists(const std::filesystem::path& path) {
    std::error_code ec;
    return std::filesystem::is_directory(path, ec);
}

bool FileExists(const std::filesystem::path& path) {
    std::error_code ec;
    return std::filesystem::is_regular_file(path, ec);
}

std::filesystem::path ExeDirectory() {
#ifdef _WIN32
    std::wstring buffer(MAX_PATH, L'\0');
    DWORD length = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
    while (length == buffer.size()) {
        buffer.resize(buffer.size() * 2, L'\0');
        length = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
    }
    if (length > 0) {
        buffer.resize(length);
        return std::filesystem::path(buffer).parent_path();
    }
#endif
    std::error_code ec;
    return std::filesystem::current_path(ec);
}

std::vector<std::string> ReadStringArray(const nlohmann::json& value) {
    std::vector<std::string> result;
    if (!value.is_array()) {
        return result;
    }
    for (const nlohmann::json& item : value) {
        if (item.is_string()) {
            result.push_back(item.get<std::string>());
        }
    }
    return result;
}

} // namespace

NeuralDenoiseManager& NeuralDenoiseManager::Instance() {
    static NeuralDenoiseManager manager;
    return manager;
}

NeuralDenoiseManager::NeuralDenoiseManager() {
    m_RootDirectory = ResolveDenoiseRoot();
    m_ManifestPath = m_RootDirectory / "manifest.json";
}

void NeuralDenoiseManager::EnsureInitialized() {
    if (!m_Initialized) {
        Refresh();
    }
}

void NeuralDenoiseManager::Refresh() {
    m_RootDirectory = ResolveDenoiseRoot();
    m_ManifestPath = m_RootDirectory / "manifest.json";
    m_Initialized = true;
    m_DenoiseFolderPresent = DirectoryExists(m_RootDirectory);
    m_ManifestLoaded = false;
    m_ManifestVersion = 0;
    m_Models.clear();
    m_Warnings.clear();
    m_OnnxBackend.SetRuntimeDirectory(m_RootDirectory / "runtimes");

    if (!m_DenoiseFolderPresent) {
        m_Warnings.push_back("Denoise model pack not installed.");
        return;
    }

    LoadManifest();
}

const NeuralDenoiseModelInfo* NeuralDenoiseManager::FindModel(const std::string& id) const {
    for (const NeuralDenoiseModelInfo& model : m_Models) {
        if (model.id == id) {
            return &model;
        }
    }
    return nullptr;
}

const NeuralDenoiseModelInfo* NeuralDenoiseManager::FirstModelOfType(ModelType type) const {
    for (const NeuralDenoiseModelInfo& model : m_Models) {
        if (model.type == type) {
            return &model;
        }
    }
    return nullptr;
}

std::vector<const NeuralDenoiseModelInfo*> NeuralDenoiseManager::ModelsOfType(ModelType type) const {
    std::vector<const NeuralDenoiseModelInfo*> result;
    for (const NeuralDenoiseModelInfo& model : m_Models) {
        if (model.type == type) {
            result.push_back(&model);
        }
    }
    return result;
}

ModelAvailability NeuralDenoiseManager::GetAvailability(const NeuralDenoiseModelInfo* model) const {
    ModelAvailability availability;
    availability.manifestLoaded = m_ManifestLoaded;
    if (!m_DenoiseFolderPresent) {
        availability.status = "Denoise model pack not installed";
        availability.warnings = m_Warnings;
        return availability;
    }
    if (!m_ManifestLoaded) {
        availability.status = "Denoise manifest missing or invalid";
        availability.warnings = m_Warnings;
        return availability;
    }
    if (!model) {
        availability.status = "No denoise model selected";
        return availability;
    }

    availability.modelFileAvailable = FileExists(model->resolvedPath);
    availability.licenseNoticeAvailable = model->licenseFile.empty() || FileExists(m_RootDirectory / model->licenseFile);

    const BackendStatus onnxStatus = m_OnnxBackend.GetStatus();
    availability.runtimeAvailable = onnxStatus.available;
    availability.supported = availability.modelFileAvailable &&
        availability.runtimeAvailable &&
        m_OnnxBackend.SupportsModel(*model);

    if (!availability.modelFileAvailable) {
        availability.status = "Model file missing";
    } else if (!availability.runtimeAvailable) {
        availability.status = onnxStatus.status.empty() ? "ONNX Runtime missing" : onnxStatus.status;
    } else if (!m_OnnxBackend.SupportsModel(*model)) {
        availability.status = "Model backend unsupported";
    } else {
        availability.status = "Model available";
    }

    if (!availability.licenseNoticeAvailable) {
        availability.warnings.push_back("Declared model license notice file is missing.");
    }
    for (const std::string& warning : onnxStatus.warnings) {
        availability.warnings.push_back(warning);
    }
    return availability;
}

std::string NeuralDenoiseManager::OverallStatus() const {
    if (!m_DenoiseFolderPresent) {
        return "Denoise model pack not installed";
    }
    if (!m_ManifestLoaded) {
        return "Denoise manifest missing or invalid";
    }
    if (m_Models.empty()) {
        return "No denoise models listed in manifest";
    }
    return "Denoise manifest loaded";
}

std::filesystem::path NeuralDenoiseManager::ResolveDenoiseRoot() const {
    if (const char* env = std::getenv("STACK_DENOISE_DIR")) {
        if (env[0] != '\0') {
            return std::filesystem::path(env);
        }
    }

    const std::filesystem::path besideExe = ExeDirectory() / "denoise";
    if (DirectoryExists(besideExe)) {
        return besideExe;
    }

    std::error_code ec;
    const std::filesystem::path devPath = std::filesystem::current_path(ec) / "denoise";
    if (!ec) {
        return devPath;
    }
    return besideExe;
}

void NeuralDenoiseManager::LoadManifest() {
    if (!FileExists(m_ManifestPath)) {
        m_Warnings.push_back("Denoise manifest.json missing.");
        std::cerr << "[NeuralDenoise] Denoise manifest missing: " << m_ManifestPath.string() << "\n";
        return;
    }

    try {
        std::ifstream input(m_ManifestPath);
        nlohmann::json manifest = nlohmann::json::parse(input, nullptr, true, true);
        if (!manifest.is_object()) {
            m_Warnings.push_back("Denoise manifest root must be an object.");
            return;
        }
        m_ManifestVersion = manifest.value("version", 0);
        const nlohmann::json models = manifest.value("models", nlohmann::json::array());
        if (!models.is_array()) {
            m_Warnings.push_back("Denoise manifest models field must be an array.");
            return;
        }

        for (const nlohmann::json& item : models) {
            if (!item.is_object()) {
                continue;
            }
            NeuralDenoiseModelInfo model;
            model.id = item.value("id", std::string());
            model.displayName = item.value("displayName", model.id);
            model.relativeFile = item.value("file", std::string());
            model.resolvedPath = (m_RootDirectory / model.relativeFile).string();
            model.type = ModelTypeFromToken(item.value("type", std::string("unknown")));
            model.architecture = item.value("architecture", std::string());
            model.preferredBackend = item.value("preferredBackend", std::string());
            model.inputFormat = item.value("inputFormat", model.inputFormat);
            model.inputRange = item.value("inputRange", model.inputRange);
            model.inputName = item.value("inputName", std::string());
            model.outputName = item.value("outputName", std::string());
            model.precision = ReadStringArray(item.value("precision", nlohmann::json::array()));
            model.inputChannels = item.value("inputChannels", 0);
            model.outputChannels = item.value("outputChannels", 0);
            model.supportsTiling = item.value("supportsTiling", false);
            model.requiredInputMultiple = std::clamp(item.value("requiredInputMultiple", model.requiredInputMultiple), 1, 512);
            model.license = item.value("license", std::string());
            model.licenseFile = item.value("licenseFile", std::string());
            model.tileHints.tileSize = std::clamp(item.value("tileSize", model.tileHints.tileSize), 64, 4096);
            model.tileHints.overlap = std::clamp(item.value("tileOverlap", model.tileHints.overlap), 0, model.tileHints.tileSize / 2);
            const nlohmann::json tiling = item.value("tiling", nlohmann::json::object());
            if (tiling.is_object()) {
                model.tileHints.tileSize = std::clamp(tiling.value("tileSize", model.tileHints.tileSize), 64, 4096);
                model.tileHints.overlap = std::clamp(tiling.value("overlap", model.tileHints.overlap), 0, model.tileHints.tileSize / 2);
                model.tileHints.featherMerge = tiling.value("featherMerge", model.tileHints.featherMerge);
            }
            if (model.id.empty() || model.relativeFile.empty()) {
                m_Warnings.push_back("Skipping denoise manifest model with missing id or file.");
                continue;
            }
            m_Models.push_back(std::move(model));
        }

        m_ManifestLoaded = true;
        if (m_ManifestVersion != 1) {
            m_Warnings.push_back("Denoise manifest version is not 1; attempting best-effort load.");
        }
    } catch (const std::exception& e) {
        m_Warnings.push_back(std::string("Denoise manifest parse failed: ") + e.what());
        std::cerr << "[NeuralDenoise] Manifest parse failed: " << e.what() << "\n";
    }
}

} // namespace NeuralDenoise
