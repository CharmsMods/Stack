#include "LibraryManager.h"

#include "Async/TaskSystem.h"
#include "Editor/EditorModule.h"
#include "Library/Internal/LibraryImageHelpers.h"
#include "Library/Internal/LibraryStorageHelpers.h"
#include "Renderer/GLHelpers.h"

#include <algorithm>
#include <ctime>
#include <filesystem>
#include <iostream>
#include <system_error>
#include <utility>

namespace {

namespace StackFormat = StackBinaryFormat;
namespace LibraryImage = Stack::Library::ImageHelpers;

} // namespace

using namespace Stack::Library::StorageHelpers;

void LibraryManager::PrepareConflictPreview(int index) {
    if (index < 0 || index >= (int)m_PendingConflicts.size()) return;
    auto& conflict = m_PendingConflicts[index];
    if (conflict.previewsReady || conflict.previewFailed) return;

    if (conflict.importedProjectIndex < 0 || conflict.importedProjectIndex >= (int)m_ActiveImportBundle.projects.size()) {
        return;
    }

    const auto& importedProjectEntry = m_ActiveImportBundle.projects[conflict.importedProjectIndex];
    const auto& importedProjectObj = importedProjectEntry.project;

    StackFormat::ProjectLoadOptions loadOptions;
    loadOptions.includeThumbnail = true;
    loadOptions.includeSourceImage = true;
    loadOptions.includePipelineData = true;

    StackFormat::ProjectDocument localProjectObj;
    if (!LoadProjectDocument(conflict.localProjectFileName, localProjectObj, loadOptions)) {
        return;
    }

    std::vector<unsigned char> importedAssetBytes;
    for (const auto& asset : m_ActiveImportBundle.assets) {
        if (asset.projectFileName == importedProjectEntry.fileName && !asset.imageBytes.empty()) {
            importedAssetBytes = asset.imageBytes;
            break;
        }
    }
    const std::filesystem::path localAssetPath = BuildAssetPathForProjectFile(conflict.localProjectFileName);

    std::vector<unsigned char> localPixels;
    int localW = 0;
    int localH = 0;
    std::string localStatus;
    const bool localPreviewOk = LibraryImage::ResolveProjectPreviewPixels(
        localProjectObj,
        &localAssetPath,
        nullptr,
        localPixels,
        localW,
        localH,
        localStatus);

    std::vector<unsigned char> importedPixels;
    int importedW = 0;
    int importedH = 0;
    std::string importedStatus;
    const bool importedPreviewOk = LibraryImage::ResolveProjectPreviewPixels(
        importedProjectObj,
        nullptr,
        importedAssetBytes.empty() ? nullptr : &importedAssetBytes,
        importedPixels,
        importedW,
        importedH,
        importedStatus);

    if (localPreviewOk && !localPixels.empty()) {
        conflict.localPreviewTex = GLHelpers::CreateTextureFromPixels(localPixels.data(), localW, localH, 4);
    }
    if (importedPreviewOk && !importedPixels.empty()) {
        conflict.importedPreviewTex = GLHelpers::CreateTextureFromPixels(importedPixels.data(), importedW, importedH, 4);
    }

    conflict.previewsReady = conflict.localPreviewTex != 0 && conflict.importedPreviewTex != 0;
    conflict.previewFailed = !conflict.previewsReady;
    if (conflict.previewFailed) {
        conflict.previewStatusText = "Preview generation failed.";
        if (!localStatus.empty()) {
            conflict.previewStatusText += "\nLocal: " + localStatus;
        }
        if (!importedStatus.empty()) {
            conflict.previewStatusText += "\nImported: " + importedStatus;
        }
        std::cerr << "[LibraryManager] Preview generation failed for conflict at index " << index << "\n";
    } else {
        conflict.previewStatusText.clear();
    }
}

void LibraryManager::ResetConflictPreview(int index) {
    if (index < 0 || index >= static_cast<int>(m_PendingConflicts.size())) {
        return;
    }

    auto& conflict = m_PendingConflicts[static_cast<std::size_t>(index)];
    if (conflict.localPreviewTex) {
        m_DeferredTextureDeletions.push_back(conflict.localPreviewTex);
        conflict.localPreviewTex = 0;
    }
    if (conflict.importedPreviewTex) {
        m_DeferredTextureDeletions.push_back(conflict.importedPreviewTex);
        conflict.importedPreviewTex = 0;
    }
    conflict.previewsReady = false;
    conflict.previewFailed = false;
    conflict.previewStatusText.clear();
}

bool LibraryManager::ExportProject(const std::string& fileName, const std::string& destinationPath) {
    if (fileName.empty() || destinationPath.empty()) return false;

    try {
        const std::filesystem::path sourcePath = m_LibraryPath / fileName;
        if (!std::filesystem::exists(sourcePath)) return false;

        const std::filesystem::path destination = destinationPath;
        if (destination.has_parent_path()) {
            std::filesystem::create_directories(destination.parent_path());
        }

        std::filesystem::copy_file(sourcePath, destination, std::filesystem::copy_options::overwrite_existing);
        QueueUiNotification(UiNotificationSeverity::Success, "Project exported.", "library-export-project");
        return true;
    } catch (...) {
        QueueUiNotification(UiNotificationSeverity::Error, "Failed to export the project.", "library-export-project");
        return false;
    }
}

void LibraryManager::RequestExportLibraryBundle(const std::string& destinationPath) {
    if (destinationPath.empty() || Async::IsBusy(m_ExportTaskState)) return;

    ++m_ExportGeneration;
    const std::uint64_t generation = m_ExportGeneration;
    m_ExportTaskState = Async::TaskState::Running;
    m_ExportStatusText = "Exporting the library bundle in the background...";

    Async::TaskSystem::Get().Submit([this, generation, destinationPath]() {
        const bool success = WriteLibraryBundle(destinationPath);
        Async::TaskSystem::Get().PostToMain([this, generation, success]() {
            if (generation != m_ExportGeneration) {
                return;
            }

            if (success) {
                m_ExportTaskState = Async::TaskState::Idle;
                m_ExportStatusText = "Library export completed.";
                QueueUiNotification(UiNotificationSeverity::Success, "Library export completed.", "library-export-bundle");
            } else {
                m_ExportTaskState = Async::TaskState::Failed;
                m_ExportStatusText = "Failed to export the library bundle.";
                QueueUiNotification(UiNotificationSeverity::Error, "Failed to export the library bundle.", "library-export-bundle");
            }
        });
    });
}

void LibraryManager::ClearConflicts() {
    for (auto& conflict : m_PendingConflicts) {
        if (conflict.localPreviewTex) {
            m_DeferredTextureDeletions.push_back(conflict.localPreviewTex);
        }
        if (conflict.importedPreviewTex) {
            m_DeferredTextureDeletions.push_back(conflict.importedPreviewTex);
        }
    }
    m_PendingConflicts.clear();
    m_ActiveImportBundle = {};
}

void LibraryManager::ResolveConflict(int index, ConflictAction action, const std::string& newName) {
    if (index < 0 || index >= (int)m_PendingConflicts.size()) return;

    auto& conflict = m_PendingConflicts[index];
    bool resolved = false;

    if (conflict.importedProjectIndex < 0 || conflict.importedProjectIndex >= (int)m_ActiveImportBundle.projects.size()) {
        m_PendingConflicts.erase(m_PendingConflicts.begin() + index);
        return;
    }

    const auto& importedBundledProject = m_ActiveImportBundle.projects[conflict.importedProjectIndex];

    if (action == ConflictAction::Ignore) {
        resolved = true;
    } else if (action == ConflictAction::Replace) {
        const std::string fileName = EnsureProjectFileNameForKind(
            importedBundledProject.fileName,
            SanitizeFileStem(importedBundledProject.project.metadata.projectName),
            importedBundledProject.project.metadata.projectKind);
        if (StackFormat::WriteProjectFile(m_LibraryPath / fileName, importedBundledProject.project)) {
            resolved = true;
        }
    } else if (action == ConflictAction::KeepBoth) {
        std::string targetName = newName.empty() ? importedBundledProject.project.metadata.projectName + " (Imported)" : newName;
        std::string fileName = EnsureProjectFileNameForKind(
            std::string(),
            SanitizeFileStem(targetName) + "_" + std::to_string(std::time(nullptr)),
            importedBundledProject.project.metadata.projectKind);

        StackFormat::ProjectDocument doc = importedBundledProject.project;
        doc.metadata.projectName = targetName;
        doc.metadata.timestamp = BuildTimestampString();

        if (StackFormat::WriteProjectFile(m_LibraryPath / fileName, doc)) {
            resolved = true;
        }
    }

    if (resolved) {
        if (conflict.localPreviewTex) m_DeferredTextureDeletions.push_back(conflict.localPreviewTex);
        if (conflict.importedPreviewTex) m_DeferredTextureDeletions.push_back(conflict.importedPreviewTex);
        m_PendingConflicts.erase(m_PendingConflicts.begin() + index);

        if (m_PendingConflicts.empty()) {
            m_ImportStatusText = "Conflict resolution completed.";
            m_LastLibrarySignature = 0;
            RequestRefreshLibraryAsync();
        }
    }
}

void LibraryManager::RequestImportLibraryBundle(const std::string& sourcePath) {
    if (sourcePath.empty() || Async::IsBusy(m_ImportTaskState)) return;

    ++m_ImportGeneration;
    const std::uint64_t generation = m_ImportGeneration;
    m_ImportTaskState = Async::TaskState::Running;
    m_ImportStatusText = "Importing the library bundle in the background...";

    Async::TaskSystem::Get().Submit([this, generation, sourcePath]() {
        const bool success = ImportLibraryBundle(sourcePath);
        Async::TaskSystem::Get().PostToMain([this, generation, success]() {
            if (generation != m_ImportGeneration) {
                return;
            }

            if (success) {
                m_ImportTaskState = Async::TaskState::Idle;
                m_ImportStatusText = "Library import completed.";
                QueueUiNotification(UiNotificationSeverity::Success, "Library import completed.", "library-import-bundle");
                m_LastLibrarySignature = 0;
                RequestRefreshLibraryAsync();
            } else {
                m_ImportTaskState = Async::TaskState::Failed;
                m_ImportStatusText = "Failed to import the library bundle.";
                QueueUiNotification(UiNotificationSeverity::Error, "Failed to import the library bundle.", "library-import-bundle");
            }
        });
    });
}

bool LibraryManager::WriteLibraryBundle(const std::string& destinationPath) {
    if (destinationPath.empty()) return false;

    try {
        StackFormat::LibraryBundleDocument bundle;
        bundle.timestamp = BuildTimestampString();

        const StackFormat::ProjectLoadOptions fullProjectLoad {
            true,
            true,
            true
        };

        for (const auto& entry : std::filesystem::directory_iterator(m_LibraryPath)) {
            if (!IsSupportedProjectExtension(entry.path())) continue;

            StackFormat::ProjectDocument document;
            if (!LoadProjectDocument(entry.path().filename().string(), document, fullProjectLoad)) {
                continue;
            }

            StackFormat::BundledProjectDocument bundledProject;
            bundledProject.fileName = entry.path().filename().string();
            bundledProject.project = std::move(document);
            bundle.projects.push_back(std::move(bundledProject));
        }

        for (const auto& entry : std::filesystem::directory_iterator(m_AssetsPath)) {
            if (!IsSupportedAssetExtension(entry.path())) continue;

            StackFormat::AssetDocument asset;
            asset.fileName = entry.path().filename().string();
            asset.displayName = DisplayNameFromStem(entry.path().stem().string());
            asset.projectFileName = ResolveAssetProjectFileName(entry.path().stem().string(), bundle.projects);

            const auto projectIt = std::find_if(
                bundle.projects.begin(),
                bundle.projects.end(),
                [&](const StackFormat::BundledProjectDocument& project) {
                    return !asset.projectFileName.empty() && project.fileName == asset.projectFileName;
                });

            if (projectIt != bundle.projects.end()) {
                if (projectIt->project.metadata.projectKind != StackFormat::kCompositeProjectKind) {
                    asset.projectName = projectIt->project.metadata.projectName;
                    asset.displayName = projectIt->project.metadata.projectName;
                } else {
                    asset.projectFileName.clear();
                }
            }

            int width = 0;
            int height = 0;
            int channels = 0;
            if (LibraryImage::ReadImageInfo(entry.path(), width, height, channels)) {
                asset.width = width;
                asset.height = height;
            }

            std::error_code ec;
            const auto writeTime = std::filesystem::last_write_time(entry.path(), ec);
            asset.timestamp = ec ? "Unknown" : BuildTimestampStringFromFileTime(writeTime);

            if (!ReadFileBytes(entry.path(), asset.imageBytes)) {
                continue;
            }

            bundle.assets.push_back(std::move(asset));
        }

        return StackFormat::WriteLibraryBundle(destinationPath, bundle);
    } catch (...) {
        return false;
    }
}

bool LibraryManager::ImportLibraryBundle(const std::string& sourcePath) {
    if (sourcePath.empty()) return false;

    try {
        StackFormat::LibraryBundleDocument bundle;
        if (!StackFormat::ReadLibraryBundle(sourcePath, bundle)) {
            return false;
        }

        std::vector<ImportConflict> conflicts;
        std::vector<int> immediateIndices;

        for (int i = 0; i < (int)bundle.projects.size(); ++i) {
            const auto& project = bundle.projects[i];
            const std::string fileName = EnsureProjectFileNameForKind(
                project.fileName,
                SanitizeFileStem(project.project.metadata.projectName),
                project.project.metadata.projectKind);

            if (std::filesystem::exists(m_LibraryPath / fileName)) {
                ImportConflict conflict;
                conflict.importedProjectIndex = i;
                conflict.localProjectFileName = fileName;

                conflict.importedName = project.project.metadata.projectName;
                conflict.importedTimestamp = project.project.metadata.timestamp;
                conflict.importedWidth = project.project.metadata.sourceWidth;
                conflict.importedHeight = project.project.metadata.sourceHeight;

                StackFormat::ProjectLoadOptions options;
                options.includeThumbnail = true;
                options.includeSourceImage = true;
                options.includePipelineData = true;

                StackFormat::ProjectDocument localDoc;
                if (LoadProjectDocument(fileName, localDoc, options)) {
                    conflict.areIdentical = StackFormat::AreProjectsIdentical(project.project, localDoc);

                    conflict.localName = localDoc.metadata.projectName;
                    conflict.localTimestamp = localDoc.metadata.timestamp;
                    conflict.localWidth = localDoc.metadata.sourceWidth;
                    conflict.localHeight = localDoc.metadata.sourceHeight;
                } else {
                    conflict.areIdentical = false;
                    conflict.localName = fileName;
                    conflict.localTimestamp = "Unknown";
                }
                conflicts.push_back(std::move(conflict));
            } else {
                immediateIndices.push_back(i);
            }
        }

        if (conflicts.empty()) {
            FinalizeImport(bundle, {});
            return true;
        } else {
            Async::TaskSystem::Get().PostToMain([this, bundle = std::move(bundle), conflicts = std::move(conflicts)]() mutable {
                ClearConflicts();
                m_ActiveImportBundle = std::move(bundle);
                m_PendingConflicts = std::move(conflicts);
                m_ImportTaskState = Async::TaskState::Idle;
                m_ImportStatusText = "Conflicts detected. Please resolve them to complete the import.";
            });
            return true;
        }
    } catch (...) {
        return false;
    }
}

void LibraryManager::FinalizeImport(const StackFormat::LibraryBundleDocument& bundle, const std::vector<int>& skippedProjectIndices) {
    if (!std::filesystem::exists(m_LibraryPath)) {
        std::filesystem::create_directories(m_LibraryPath);
    }
    if (!std::filesystem::exists(m_AssetsPath)) {
        std::filesystem::create_directories(m_AssetsPath);
    }

    for (int i = 0; i < (int)bundle.projects.size(); ++i) {
        bool skip = false;
        for (int skipIdx : skippedProjectIndices) {
            if (i == skipIdx) {
                skip = true;
                break;
            }
        }
        if (skip) continue;

        const auto& project = bundle.projects[i];
        const std::string fileName = EnsureProjectFileNameForKind(
            project.fileName,
            SanitizeFileStem(project.project.metadata.projectName),
            project.project.metadata.projectKind);

        StackFormat::WriteProjectFile(m_LibraryPath / fileName, project.project);
    }

    for (const auto& asset : bundle.assets) {
        const std::string fallbackName = std::filesystem::path(asset.projectFileName.empty() ? "imported_asset" : asset.projectFileName).stem().string() + ".png";
        const std::string fileName = EnsureAssetFileName(asset.fileName, fallbackName);
        WriteFileBytes(m_AssetsPath / fileName, asset.imageBytes);
    }
}

void LibraryManager::RequestImportAndLoad(
    const std::string& sourcePath,
    EditorModule* editor,
    CompositeModule* composite,
    std::function<void(int)> onTabSwitchRequested) {
    (void)composite;
    if (sourcePath.empty()) return;

    const std::filesystem::path path(sourcePath);
    if (!std::filesystem::exists(path)) return;

    if (path.extension() == ".stacklib") {
        RequestImportLibraryBundle(sourcePath);
        return;
    }

    if (IsSupportedProjectExtension(path)) {
        StackFormat::ProjectDocument document;
        StackFormat::ProjectLoadOptions metadataOnly { true, false, false };
        bool success = false;
        if (StackFormat::ReadProjectFile(path, document, metadataOnly)) {
            success = true;
        } else {
            success = LoadLegacyProjectDocument(path, document, metadataOnly);
        }

        if (!success) {
            m_ImportStatusText = "Failed to read project file metadata.";
            m_ImportTaskState = Async::TaskState::Failed;
            QueueUiNotification(UiNotificationSeverity::Error, "Failed to read project file metadata.", "library-import-project");
            return;
        }

        if (document.metadata.projectKind == StackFormat::kCompositeProjectKind) {
            m_ImportStatusText = "Legacy standalone composite projects are no longer supported.";
            m_ImportTaskState = Async::TaskState::Failed;
            QueueUiNotification(UiNotificationSeverity::Error, "Legacy standalone composite projects are no longer supported.", "library-import-project");
            return;
        }

        const std::string fileName = EnsureProjectFileNameForKind(
            path.filename().string(),
            SanitizeFileStem(document.metadata.projectName),
            document.metadata.projectKind);
        if (std::filesystem::exists(m_LibraryPath / fileName)) {
            m_ImportStatusText = "Project already exists in library. Resolving conflict...";

            ImportConflict conflict;
            conflict.importedProjectIndex = 0;
            conflict.localProjectFileName = fileName;
            conflict.importedName = document.metadata.projectName;
            conflict.importedTimestamp = document.metadata.timestamp;
            conflict.importedWidth = document.metadata.sourceWidth;
            conflict.importedHeight = document.metadata.sourceHeight;

            StackFormat::ProjectDocument localDoc;
            StackFormat::ProjectLoadOptions fullLoad { true, true, true };
            if (LoadProjectDocument(fileName, localDoc, fullLoad)) {
                conflict.areIdentical = StackFormat::AreProjectsIdentical(document, localDoc);
                conflict.localName = localDoc.metadata.projectName;
                conflict.localTimestamp = localDoc.metadata.timestamp;
                conflict.localWidth = localDoc.metadata.sourceWidth;
                conflict.localHeight = localDoc.metadata.sourceHeight;
            } else {
                conflict.areIdentical = false;
                conflict.localName = fileName;
            }

            if (!StackFormat::ReadProjectFile(path, document, fullLoad)) {
                LoadLegacyProjectDocument(path, document, fullLoad);
            }

            StackFormat::LibraryBundleDocument bundle;
            bundle.timestamp = BuildTimestampString();
            StackFormat::BundledProjectDocument bundled;
            bundled.fileName = fileName;
            bundled.project = std::move(document);
            bundle.projects.push_back(std::move(bundled));

            ClearConflicts();
            m_ActiveImportBundle = std::move(bundle);
            m_PendingConflicts.push_back(std::move(conflict));
            m_ImportTaskState = Async::TaskState::Idle;
            return;
        }

        std::error_code ec;
        std::filesystem::copy_file(path, m_LibraryPath / fileName, std::filesystem::copy_options::overwrite_existing, ec);
        if (ec) {
            m_ImportStatusText = "Failed to copy project to library: " + ec.message();
            m_ImportTaskState = Async::TaskState::Failed;
            QueueUiNotification(UiNotificationSeverity::Error, m_ImportStatusText, "library-import-project");
            return;
        }

        m_LastLibrarySignature = 0;
        RequestRefreshLibraryAsync();

        if (document.metadata.projectKind == StackFormat::kRenderProjectKind) {
            m_ImportStatusText = "Render projects are no longer supported.";
            m_ImportTaskState = Async::TaskState::Failed;
            QueueUiNotification(UiNotificationSeverity::Error, "Render projects are no longer supported.", "library-import-project");
            return;
        } else {
            if (document.metadata.projectKind == StackFormat::kCompositeProjectKind) {
                m_ImportStatusText = "Legacy standalone composite projects are no longer supported.";
                m_ImportTaskState = Async::TaskState::Failed;
                QueueUiNotification(UiNotificationSeverity::Error, "Legacy standalone composite projects are no longer supported.", "library-import-project");
                return;
            }
            if (onTabSwitchRequested) onTabSwitchRequested(1);
            RequestLoadProjectFromPath(m_LibraryPath / fileName, editor);
        }
        return;
    }

    if (IsSupportedAssetExtension(path)) {
        const std::string fileName = EnsureAssetFileName(path.filename().string(), path.filename().string());
        std::error_code ec;
        std::filesystem::copy_file(path, m_AssetsPath / fileName, std::filesystem::copy_options::overwrite_existing, ec);
        if (ec) {
            m_ImportStatusText = "Failed to copy image to library: " + ec.message();
            m_ImportTaskState = Async::TaskState::Failed;
            QueueUiNotification(UiNotificationSeverity::Error, m_ImportStatusText, "library-import-asset");
            return;
        }

        m_LastLibrarySignature = 0;
        RequestRefreshLibraryAsync();

        if (editor) {
            if (onTabSwitchRequested) onTabSwitchRequested(1);
            editor->RequestLoadSourceImage((m_AssetsPath / fileName).string());
        }
        return;
    }

    m_ImportStatusText = "Unsupported file type dropped.";
    m_ImportTaskState = Async::TaskState::Failed;
    QueueUiNotification(UiNotificationSeverity::Error, "Unsupported file type dropped.", "library-import-asset");
}
