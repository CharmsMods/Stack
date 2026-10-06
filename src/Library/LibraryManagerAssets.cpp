#include "LibraryManager.h"

#include "Async/TaskSystem.h"
#include "Library/Internal/LibraryImageHelpers.h"
#include "Library/Internal/LibraryStorageHelpers.h"
#include "Library/TagManager.h"
#include "Renderer/GLHelpers.h"

#include <algorithm>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <system_error>

namespace {

namespace StackFormat = StackBinaryFormat;
namespace LibraryImage = Stack::Library::ImageHelpers;

} // namespace

using namespace Stack::Library::StorageHelpers;

void LibraryManager::ClearAssetConflicts() {
    for (auto& conflict : m_PendingAssetConflicts) {
        m_Notifier.CancelActivity(conflict.activity, "Asset import stopped.");
        if (conflict.localPreviewTex) {
            m_DeferredTextureDeletions.push_back(conflict.localPreviewTex);
        }
        if (conflict.importedPreviewTex) {
            m_DeferredTextureDeletions.push_back(conflict.importedPreviewTex);
        }
    }
    m_PendingAssetConflicts.clear();
}

void LibraryManager::PrepareAssetConflictPreview(int index) {
    if (index < 0 || index >= static_cast<int>(m_PendingAssetConflicts.size())) {
        return;
    }

    auto& conflict = m_PendingAssetConflicts[index];
    if (conflict.previewsReady) {
        return;
    }

    std::vector<unsigned char> localPixels;
    int localW = 0;
    int localH = 0;
    if (LibraryImage::LoadRgbaImageFromFile(m_AssetsPath / conflict.localAssetFileName, localPixels, localW, localH)) {
        conflict.localPreviewTex = GLHelpers::CreateTextureFromPixels(localPixels.data(), localW, localH, 4);
    }

    std::vector<unsigned char> importedPixels;
    int importedW = 0;
    int importedH = 0;
    int importedC = 4;
    if (DecodeImageBytes(conflict.importedImageBytes, importedPixels, importedW, importedH, importedC)) {
        conflict.importedPreviewTex = GLHelpers::CreateTextureFromPixels(importedPixels.data(), importedW, importedH, 4);
    }

    conflict.previewsReady = true;
}

int LibraryManager::FindAssetConflictIndex(std::uint64_t id) const {
    for (std::size_t i = 0; i < m_PendingAssetConflicts.size(); ++i) {
        if (m_PendingAssetConflicts[i].id == id) return static_cast<int>(i);
    }
    return -1;
}

ConflictResolutionResult LibraryManager::ResolveAssetConflict(std::uint64_t id, AssetConflictAction action) {
    const int index = FindAssetConflictIndex(id);
    if (index < 0) return { ConflictResolutionState::Failed, "This asset import is no longer waiting for a decision." };

    auto conflict = m_PendingAssetConflicts[static_cast<std::size_t>(index)];
    bool resolved = false;

    auto buildUniqueAssetFileName = [this](const std::string& preferredStem) {
        const std::string safeStem = SanitizeFileStem(preferredStem.empty() ? "imported_asset" : preferredStem);
        std::string candidate = safeStem + ".png";
        int suffix = 1;
        while (std::filesystem::exists(m_AssetsPath / candidate)) {
            candidate = safeStem + "_" + std::to_string(std::time(nullptr)) + "_" + std::to_string(suffix++) + ".png";
        }
        return candidate;
    };

    try {
    if (action == AssetConflictAction::UseExisting) {
        resolved = true;
    } else if (action == AssetConflictAction::Replace) {
        resolved = WriteFileBytes(m_AssetsPath / conflict.localAssetFileName, conflict.importedImageBytes);
    } else if (action == AssetConflictAction::KeepBoth) {
        const std::string baseStem = std::filesystem::path(
            conflict.importedAssetFileName.empty() ? conflict.importedDisplayName : conflict.importedAssetFileName).stem().string();
        const std::string targetFileName = buildUniqueAssetFileName(baseStem);
        resolved = WriteFileBytes(m_AssetsPath / targetFileName, conflict.importedImageBytes);
    }
    } catch (const std::exception& error) {
        return { ConflictResolutionState::Failed, std::string("Could not save the imported asset. ") + error.what() };
    } catch (...) {
        return { ConflictResolutionState::Failed, "Could not save the imported asset. Check the destination and try again." };
    }

    if (!resolved) {
        return { ConflictResolutionState::Failed, "Could not save the imported asset. Check that the Library folder is writable, then try again." };
    }

    if (conflict.localPreviewTex) {
        m_DeferredTextureDeletions.push_back(conflict.localPreviewTex);
    }
    if (conflict.importedPreviewTex) {
        m_DeferredTextureDeletions.push_back(conflict.importedPreviewTex);
    }

    m_PendingAssetConflicts.erase(m_PendingAssetConflicts.begin() + index);
    if (action != AssetConflictAction::UseExisting) {
        m_LastLibrarySignature = 0;
        RequestRefreshLibraryAsync();
    }
    m_Notifier.CompleteActivity(conflict.activity, action == AssetConflictAction::UseExisting
        ? "Existing asset kept." : "Asset imported.");
    return { ConflictResolutionState::Resolved, {} };
}

std::vector<std::string> LibraryManager::SyncProjectAssets(
    const std::string& projectFileName,
    const StackFormat::ProjectDocument& document) {
    std::vector<std::string> syncedFileNames;

    if (projectFileName.empty()) {
        return syncedFileNames;
    }

    std::filesystem::path projectPath(projectFileName);
    std::string projectStem = projectPath.stem().string();

    if (!document.sourceImageBytes.empty()) {
        std::string hash = ComputeImageHash(document.sourceImageBytes);
        std::string assetFileName = projectStem + ".png";
        std::filesystem::path pngPath = m_AssetsPath / assetFileName;
        std::filesystem::path hashPath = m_AssetsPath / (assetFileName + ".hash");

        bool needWrite = true;
        if (std::filesystem::exists(pngPath) && std::filesystem::exists(hashPath)) {
            try {
                std::ifstream f(hashPath);
                if (f.is_open()) {
                    nlohmann::json meta;
                    f >> meta;
                    if (meta.value("hash", "") == hash && meta.value("projectFileName", "") == projectFileName) {
                        needWrite = false;
                    }
                }
            } catch (...) {}
        }

        if (needWrite) {
            WriteFileBytes(pngPath, document.sourceImageBytes);

            int w = 0;
            int h = 0;
            int c = 4;
            std::vector<unsigned char> pixels;
            DecodeImageBytes(document.sourceImageBytes, pixels, w, h, c);

            nlohmann::json meta = {
                {"hash", hash},
                {"projectFileName", projectFileName},
                {"displayName", document.metadata.projectName.empty() ? projectStem : document.metadata.projectName},
                {"timestamp", document.metadata.timestamp},
                {"width", w},
                {"height", h}
            };
            std::ofstream f(hashPath);
            if (f.is_open()) {
                f << meta.dump(4);
            }
        }

        syncedFileNames.push_back(assetFileName);
    }

    if (document.pipelineData.is_object()) {
        const nlohmann::json& graphJson = document.pipelineData.value("nodeGraph", nlohmann::json::object());
        const auto& nodes = graphJson.value("nodes", nlohmann::json::array());
        for (const auto& item : nodes) {
            if (item.value("kind", "") == "Image" && item.contains("pngBytes")) {
                std::vector<unsigned char> pngBytes = LibraryImage::ReadBinaryJsonBytes(item.at("pngBytes"));
                if (!pngBytes.empty()) {
                    std::string hash = ComputeImageHash(pngBytes);
                    std::string label = item.value("label", item.value("title", "Image"));
                    std::string sanitizedLabel = SanitizeFileStem(label);
                    if (sanitizedLabel.empty()) {
                        sanitizedLabel = "image";
                    }

                    std::string hashPrefix = hash.substr(0, 8);
                    std::string assetFileName = projectStem + "_" + sanitizedLabel + "_" + hashPrefix + ".png";
                    std::filesystem::path pngPath = m_AssetsPath / assetFileName;
                    std::filesystem::path hashPath = m_AssetsPath / (assetFileName + ".hash");

                    bool needWrite = true;
                    if (std::filesystem::exists(pngPath) && std::filesystem::exists(hashPath)) {
                        try {
                            std::ifstream f(hashPath);
                            if (f.is_open()) {
                                nlohmann::json meta;
                                f >> meta;
                                if (meta.value("hash", "") == hash && meta.value("projectFileName", "") == projectFileName) {
                                    needWrite = false;
                                }
                            }
                        } catch (...) {}
                    }

                    if (needWrite) {
                        WriteFileBytes(pngPath, pngBytes);

                        int w = 0;
                        int h = 0;
                        int c = 4;
                        std::vector<unsigned char> pixels;
                        DecodeImageBytes(pngBytes, pixels, w, h, c);

                        nlohmann::json meta = {
                            {"hash", hash},
                            {"projectFileName", projectFileName},
                            {"displayName", label},
                            {"timestamp", document.metadata.timestamp},
                            {"width", w},
                            {"height", h}
                        };
                        std::ofstream f(hashPath);
                        if (f.is_open()) {
                            f << meta.dump(4);
                        }
                    }

                    syncedFileNames.push_back(assetFileName);
                }
            }
        }
    }

    return syncedFileNames;
}

void LibraryManager::CleanupOrphanedAssets(const std::vector<std::string>& activeAssetFileNames) {
    if (!std::filesystem::exists(m_AssetsPath)) {
        return;
    }

    std::error_code ec;
    for (const auto& entry : std::filesystem::directory_iterator(m_AssetsPath, ec)) {
        if (!entry.is_regular_file()) {
            continue;
        }

        std::string filename = entry.path().filename().string();

        std::string checkName = filename;
        if (filename.size() > 5 && filename.substr(filename.size() - 5) == ".hash") {
            checkName = filename.substr(0, filename.size() - 5);
        }

        if (std::find(activeAssetFileNames.begin(), activeAssetFileNames.end(), checkName) == activeAssetFileNames.end()) {
            std::filesystem::remove(entry.path(), ec);
        }
    }
}

void LibraryManager::QueueLooseAssetSave(
    const std::string& displayName,
    const std::vector<unsigned char>& imageBytes,
    const std::string& preferredFileName,
    const std::string& projectFileName,
    const std::string& projectName,
    const std::string& projectKind,
    bool meaningful) {

    if (imageBytes.empty()) {
        if (meaningful) m_Notifier.Error("No image data is available to import.");
        return;
    }

    const std::string trimmedDisplayName = TrimWhitespace(displayName).empty() ? "Imported Asset" : TrimWhitespace(displayName);
    const std::string fallbackFileName = SanitizeFileStem(trimmedDisplayName) + ".png";
    const std::string resolvedPreferredFileName = EnsureAssetFileName(preferredFileName, fallbackFileName);
    const auto notifier = m_Notifier;
    Stack::Notifications::NoticeSpec work;
    work.title = "Importing asset";
    work.context = trimmedDisplayName;
    const auto inherited = Async::TaskSystem::CurrentActivity();
    if (inherited.ownerId == notifier.GetOwner().id && inherited.ownerGeneration == notifier.GetOwner().generation)
        work.operationId = inherited.operationId;
    work.preview = false;
    work.maintenance = !meaningful;
    const auto activity = notifier.BeginActivity(std::move(work));
    Async::ActivityMetadata metadata;
    metadata.ownerId = notifier.GetOwner().id;
    metadata.ownerGeneration = notifier.GetOwner().generation;
    metadata.operationId = activity.operationId;
    metadata.label = "Loading assets";
    metadata.maintenance = !meaningful;

    const bool submitted = Async::TaskSystem::Get().Submit(std::move(metadata),[this,
                                     trimmedDisplayName,
                                     imageBytes,
                                     resolvedPreferredFileName,
                                     projectFileName,
                                     projectName,
                                     projectKind, notifier, activity, meaningful]() mutable {
        try {
        std::vector<unsigned char> importedPixels;
        int importedW = 0;
        int importedH = 0;
        int importedC = 4;
        if (!DecodeImageBytes(imageBytes, importedPixels, importedW, importedH, importedC)) {
            notifier.FailActivity(activity, "Could not read the imported image.");
            return;
        }

        std::error_code ec;
        std::filesystem::create_directories(m_AssetsPath, ec);
        if (ec) {
            notifier.FailActivity(activity, "Could not access the Library image folder.", ec.message());
            return;
        }

        const std::filesystem::path preferredPath = m_AssetsPath / resolvedPreferredFileName;
        if (std::filesystem::exists(preferredPath)) {
            std::vector<unsigned char> existingPixels;
            int existingW = 0;
            int existingH = 0;
            if (LibraryImage::LoadRgbaImageFromFile(preferredPath, existingPixels, existingW, existingH)) {
                if (existingW == importedW &&
                    existingH == importedH &&
                    existingPixels.size() == importedPixels.size() &&
                    ComputeExactPixelFingerprint(existingPixels) == ComputeExactPixelFingerprint(importedPixels) &&
                    existingPixels == importedPixels) {
                    notifier.CompleteActivity(activity, "The image is already in the Library.", false);
                    return;
                }
            }
        }

        bool foundConflict = false;
        AssetImportConflict pendingConflict;
        int bestHashDistance = std::numeric_limits<int>::max();
        float bestNameScore = -1.0f;

        for (const auto& entry : std::filesystem::directory_iterator(m_AssetsPath)) {
            if (!IsSupportedAssetExtension(entry.path())) {
                continue;
            }

            std::vector<unsigned char> localPixels;
            int localW = 0;
            int localH = 0;
            if (!LibraryImage::LoadRgbaImageFromFile(entry.path(), localPixels, localW, localH)) {
                continue;
            }

            const std::string localDisplayName = DisplayNameFromStem(entry.path().stem().string());
            bool exactMatch = false;
            if (!ShouldQueueAssetConflict(
                    localDisplayName,
                    localPixels,
                    localW,
                    localH,
                    trimmedDisplayName,
                    importedPixels,
                    importedW,
                    importedH,
                    exactMatch)) {
                continue;
            }

            const int hashDistance = CountHammingDistance64(
                ComputeAverageHash64(localPixels, localW, localH),
                ComputeAverageHash64(importedPixels, importedW, importedH));
            const float nameScore = ComputeNameSimilarityScore(localDisplayName, trimmedDisplayName);

            if (!foundConflict ||
                exactMatch ||
                hashDistance < bestHashDistance ||
                (hashDistance == bestHashDistance && nameScore > bestNameScore)) {
                foundConflict = true;
                bestHashDistance = hashDistance;
                bestNameScore = nameScore;

                pendingConflict = AssetImportConflict {};
                pendingConflict.localAssetFileName = entry.path().filename().string();
                pendingConflict.localDisplayName = localDisplayName;
                pendingConflict.localWidth = localW;
                pendingConflict.localHeight = localH;
                pendingConflict.importedAssetFileName = resolvedPreferredFileName;
                pendingConflict.importedDisplayName = trimmedDisplayName;
                pendingConflict.importedWidth = importedW;
                pendingConflict.importedHeight = importedH;
                pendingConflict.importedProjectFileName = projectFileName;
                pendingConflict.importedProjectName = projectName;
                pendingConflict.importedProjectKind = projectKind;
                pendingConflict.importedImageBytes = imageBytes;
                pendingConflict.areIdentical = exactMatch;

                std::error_code ec;
                const auto writeTime = std::filesystem::last_write_time(entry.path(), ec);
                pendingConflict.localTimestamp = ec ? "Unknown" : BuildTimestampStringFromFileTime(writeTime);
                pendingConflict.importedTimestamp = BuildTimestampString();
            }
        }

        if (foundConflict) {
            pendingConflict.activity = activity;
            Async::TaskSystem::Get().PostToMain([this, notifier, activity, conflict = std::move(pendingConflict)]() mutable {
                const auto alreadyQueued = std::find_if(
                    m_PendingAssetConflicts.begin(),
                    m_PendingAssetConflicts.end(),
                    [&](const AssetImportConflict& existing) {
                        return existing.localAssetFileName == conflict.localAssetFileName &&
                               existing.importedDisplayName == conflict.importedDisplayName &&
                               existing.importedWidth == conflict.importedWidth &&
                               existing.importedHeight == conflict.importedHeight;
                    });
                if (alreadyQueued == m_PendingAssetConflicts.end()) {
                    conflict.id = m_NextConflictId++;
                    m_PendingAssetConflicts.push_back(std::move(conflict));
                    notifier.UpdateActivity(activity, "Waiting for your choice");
                } else {
                    notifier.CompleteActivity(activity, "This image is already waiting for a decision.", false);
                }
            });
            return;
        }

        std::string targetFileName = resolvedPreferredFileName;
        int suffix = 1;
        while (std::filesystem::exists(m_AssetsPath / targetFileName)) {
            targetFileName = SanitizeFileStem(std::filesystem::path(resolvedPreferredFileName).stem().string())
                + "_" + std::to_string(std::time(nullptr)) + "_" + std::to_string(suffix++) + ".png";
        }

        if (!WriteFileBytes(m_AssetsPath / targetFileName, imageBytes)) {
            notifier.FailActivity(activity, "Could not save the imported image.");
            return;
        }

        Async::TaskSystem::Get().PostToMain([this, notifier, activity, meaningful]() {
            m_LastLibrarySignature = 0;
            RequestRefreshLibraryAsync();
            notifier.CompleteActivity(activity, "Asset imported.", meaningful);
        });
        } catch (const std::exception& error) {
            notifier.FailActivity(activity, "Could not import the image.", error.what());
        } catch (...) {
            notifier.FailActivity(activity, "Could not import the image.");
        }
    });
    if (!submitted) notifier.FailActivity(activity, "The image import could not be started.");
}

void LibraryManager::MirrorCompositeEmbeddedAssets(const StackFormat::ProjectDocument& document) {
    if (document.metadata.projectKind != StackFormat::kCompositeProjectKind || !document.pipelineData.is_object()) {
        return;
    }

    const StackFormat::json layers = document.pipelineData.value("layers", StackFormat::json::array());
    for (const auto& item : layers) {
        if (!item.is_object()) {
            continue;
        }

        const bool generatedFromImage = item.value("generatedFromImage", false);
        const bool isImageLayer = item.value("kind", std::string("image")) == "image";
        if (!isImageLayer && !generatedFromImage) {
            continue;
        }

        std::vector<unsigned char> imageBytes;
        if (item.contains("sourcePng") && item["sourcePng"].is_binary()) {
            imageBytes = item["sourcePng"].get_binary();
        } else if (item.contains("imagePng") && item["imagePng"].is_binary()) {
            imageBytes = item["imagePng"].get_binary();
        }

        if (imageBytes.empty()) {
            continue;
        }

        const std::string layerName = item.value("name", std::string("Composite Asset"));
        QueueLooseAssetSave(layerName, imageBytes, SanitizeFileStem(layerName) + ".png", {}, {}, {}, false);
    }
}

bool LibraryManager::ExportAsset(const std::string& fileName, const std::string& destinationPath) {
    if (fileName.empty() || destinationPath.empty()) return false;

    try {
        const std::filesystem::path sourcePath = m_AssetsPath / fileName;
        if (!std::filesystem::exists(sourcePath)) {
            PostNotification(UiNotificationSeverity::Error, "The image to export is no longer available.", "library-export-asset");
            return false;
        }

        const std::filesystem::path destination = destinationPath;
        if (destination.has_parent_path()) {
            std::filesystem::create_directories(destination.parent_path());
        }

        std::filesystem::copy_file(sourcePath, destination, std::filesystem::copy_options::overwrite_existing);
        PostNotification(UiNotificationSeverity::Success, "Asset exported.", "library-export-asset");
        return true;
    } catch (...) {
        PostNotification(UiNotificationSeverity::Error, "Failed to export the asset.", "library-export-asset");
        return false;
    }
}

bool LibraryManager::DeleteAsset(const std::string& fileName) {
    auto path = m_AssetsPath / fileName;
    const std::filesystem::path hashPath = m_AssetsPath / (fileName + ".hash");
    std::error_code ec;
    if (!std::filesystem::remove(path, ec)) {
        std::cerr << "[LibraryManager] Failed to delete asset: " << fileName << " (" << ec.message() << ")\n";
        return false;
    }
    std::filesystem::remove(hashPath, ec);
    ec.clear();
    TagManager::Get().SetTags(fileName, {});
    std::cout << "[LibraryManager] Deleted asset: " << fileName << "\n";
    m_LastLibrarySignature = 0;
    return true;
}
