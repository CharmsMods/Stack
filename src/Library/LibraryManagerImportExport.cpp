#include "LibraryManager.h"

#include "Async/TaskSystem.h"
#include "Editor/EditorModule.h"
#include "Library/Internal/LibraryImageHelpers.h"
#include "Library/Internal/LibraryStorageHelpers.h"
#include "Library/Internal/LibraryImportPlan.h"
#include "Library/Internal/LibraryProjectExport.h"
#include "Library/Internal/LibraryNewAssetWrite.h"
#include "Renderer/GLHelpers.h"

#include <algorithm>
#include <cctype>
#include <ctime>
#include <cstdio>
#include <filesystem>
#include <iostream>
#include <system_error>
#include <utility>

namespace {

namespace StackFormat = StackBinaryFormat;
namespace LibraryImage = Stack::Library::ImageHelpers;

Async::ActivityMetadata LibraryActivity(const Stack::Notifications::Notifier& notifier,
    Stack::Notifications::ActivityHandle activity, std::string label, bool maintenance = false) {
    Async::ActivityMetadata metadata;
    metadata.ownerId = notifier.GetOwner().id;
    metadata.ownerGeneration = notifier.GetOwner().generation;
    metadata.operationId = activity.operationId;
    metadata.label = std::move(label);
    metadata.maintenance = maintenance;
    return metadata;
}

} // namespace

using namespace Stack::Library::StorageHelpers;

void LibraryManager::RequestImportFolderAssets(std::filesystem::path path, bool png, bool jpg, bool bmp, bool tga) {
    if (path.empty()) return;
    const auto notifier = m_Notifier;
    Stack::Notifications::NoticeSpec work;
    work.title = "Reading import folder";
    work.context = path.filename().u8string();
    work.details = path.u8string();
    work.preview = false;
    const auto activity = notifier.BeginActivity(std::move(work));
    const bool submitted = Async::TaskSystem::Get().Submit(LibraryActivity(notifier, activity, "Reading folder"),
        [this, path = std::move(path), png, jpg, bmp, tga, notifier, activity] {
            std::size_t selected = 0;
            std::size_t queued = 0;
            std::size_t unreadable = 0;
            try {
                for (const auto& entry : std::filesystem::directory_iterator(path)) {
                    if (!entry.is_regular_file()) continue;
                    std::string extension = entry.path().extension().u8string();
                    std::transform(extension.begin(), extension.end(), extension.begin(), [](unsigned char value) {
                        return static_cast<char>(std::tolower(value));
                    });
                    if (!((png && extension == ".png") || (jpg && (extension == ".jpg" || extension == ".jpeg")) ||
                        (bmp && extension == ".bmp") || (tga && extension == ".tga"))) continue;
                    ++selected;
                    notifier.UpdateActivity(activity, "Reading " + entry.path().filename().u8string());
                    std::vector<unsigned char> bytes;
                    if (!ReadFileBytes(entry.path(), bytes) || bytes.empty()) { ++unreadable; continue; }
                    QueueLooseAssetSave(entry.path().stem().u8string(), bytes, entry.path().filename().u8string());
                    ++queued;
                }
                if (unreadable) {
                    notifier.FinishActivity(activity, queued ? Stack::Notifications::Outcome::Partial : Stack::Notifications::Outcome::Failure,
                        "Some folder images could not be read.", std::to_string(queued) + " queued for import; " + std::to_string(unreadable) + " unreadable.");
                } else if (!selected) {
                    notifier.CompleteActivity(activity, "No matching images found.");
                } else {
                    // Each queued asset reports its actual write or decision result.
                    notifier.CompleteActivity(activity, "Queued " + std::to_string(queued) + " images for import.", false);
                }
            } catch (const std::exception& error) {
                notifier.FinishActivity(activity, queued ? Stack::Notifications::Outcome::Partial : Stack::Notifications::Outcome::Failure,
                    "Could not finish reading the import folder.", std::to_string(queued) + " images were queued.\n" + error.what());
            } catch (...) {
                notifier.FailActivity(activity, "Could not read the import folder.");
            }
        });
    if (!submitted) notifier.FailActivity(activity, "The folder import could not be started.");
}

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
    std::string error;
    if (!Stack::Library::ExportPortableProject(m_LibraryPath / fileName,
            std::filesystem::u8path(destinationPath), error)) {
        PostNotification(UiNotificationSeverity::Error,
            error.empty() ? "Failed to export the project." : error, "library-export-project");
        return false;
    }
    PostNotification(UiNotificationSeverity::Success, "Project exported.", "library-export-project");
    return true;
}

void LibraryManager::RequestExportLibraryBundle(const std::string& destinationPath) {
    if (destinationPath.empty() || Async::IsBusy(m_ExportTaskState)) return;

    Stack::FileSave::ExportFileTarget target;
    std::string targetError;
    if (!Stack::FileSave::PrepareExportFileTarget(std::filesystem::u8path(destinationPath),
            {}, {".stacklib"}, target, targetError)) {
        m_ExportTaskState = Async::TaskState::Failed;
        m_ExportStatusText = targetError;
        PostNotification(UiNotificationSeverity::Error, targetError, "library-export");
        return;
    }

    ++m_ExportGeneration;
    const std::uint64_t generation = m_ExportGeneration;
    m_ExportTaskState = Async::TaskState::Running;
    m_ExportStatusText = "Exporting the library bundle in the background...";
    m_ExportActivity = m_Notifier.BeginActivity("Exporting library");
    const auto activity = m_ExportActivity;
    const auto notifier = m_Notifier;

    const bool submitted = Async::TaskSystem::Get().Submit(LibraryActivity(notifier, activity, "Exporting"),[this, generation, target = std::move(target), activity, notifier]() {
        std::string error;
        const bool success = WriteLibraryBundle(target, error);
        Async::TaskSystem::Get().PostToMain([this, generation, success, error, activity, notifier]() {
            if (generation != m_ExportGeneration) {
                return;
            }

            if (success) {
                m_ExportTaskState = Async::TaskState::Idle;
                m_ExportStatusText = "Library export completed.";
                notifier.CompleteActivity(activity, "Library exported.");
            } else {
                m_ExportTaskState = Async::TaskState::Failed;
                m_ExportStatusText = error.empty() ? "Failed to export the library bundle." : error;
                notifier.FailActivity(activity, m_ExportStatusText);
            }
        });
    });
    if (!submitted) {
        m_ExportTaskState = Async::TaskState::Failed;
        m_ExportStatusText = "The library export could not be started.";
        notifier.FailActivity(activity, m_ExportStatusText);
    }
}

void LibraryManager::ClearConflicts() {
    if (!m_PendingConflicts.empty()) {
        m_Notifier.CancelActivity(m_ImportActivity, "Import stopped.");
        m_ImportStatusText = "Import stopped. Already imported files remain in the Library.";
    }
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

int LibraryManager::FindConflictIndex(std::uint64_t id) const {
    for (std::size_t i = 0; i < m_PendingConflicts.size(); ++i) {
        if (m_PendingConflicts[i].id == id) return static_cast<int>(i);
    }
    return -1;
}

ConflictResolutionResult LibraryManager::ResolveConflict(std::uint64_t id, ConflictAction action, const std::string& newName) {
    const int index = FindConflictIndex(id);
    if (index < 0) return { ConflictResolutionState::Failed, "This import is no longer waiting for a decision." };

    // Copy before resolving. Erasing the vector must not invalidate the action's inputs.
    const auto conflict = m_PendingConflicts[static_cast<std::size_t>(index)];
    bool resolved = false;

    if (conflict.importedProjectIndex < 0 || conflict.importedProjectIndex >= (int)m_ActiveImportBundle.projects.size()) {
        return { ConflictResolutionState::Failed, "The imported project data is unavailable. Abort this import and try again." };
    }

    const auto& importedBundledProject = m_ActiveImportBundle.projects[conflict.importedProjectIndex];
    std::string targetFileName;

    try {
    if (action == ConflictAction::Ignore) {
        resolved = true;
    } else {
        StackFormat::ProjectDocument doc = importedBundledProject.project;
        const bool keepBoth = action == ConflictAction::KeepBoth;
        if (keepBoth) {
            const std::string targetName = newName.empty()
                ? doc.metadata.projectName + " (Imported)" : newName;
            const std::string baseName = EnsureProjectFileNameForKind({},
                SanitizeFileStem(targetName) + "_" + std::to_string(std::time(nullptr)),
                doc.metadata.projectKind);
            targetFileName = conflict.pendingCopyFileName;
            if (targetFileName.empty()) {
                targetFileName = baseName;
                unsigned int suffix = 1;
                // A free project name does not imply its preview name is free.
                while (std::filesystem::exists(m_LibraryPath / targetFileName) ||
                    std::filesystem::exists(BuildAssetPathForProjectFile(targetFileName)))
                    targetFileName = baseName + "_" + std::to_string(suffix++);
                m_PendingConflicts[static_cast<std::size_t>(index)].pendingCopyFileName = targetFileName;
            }
            doc.metadata.projectName = targetName;
            doc.metadata.timestamp = BuildTimestampString();
        } else {
            targetFileName = EnsureProjectFileNameForKind(conflict.localProjectFileName,
                SanitizeFileStem(doc.metadata.projectName), doc.metadata.projectKind);
        }
        const auto targetProject = m_LibraryPath / targetFileName;
        const auto ownPreview = BuildAssetPathForProjectFile(targetFileName);
        std::vector<Stack::Library::ImportAssetDestination> destinations;
        for (const auto& asset : m_ActiveImportBundle.assets) {
            if (asset.projectFileName != importedBundledProject.fileName) continue;
            const auto path = keepBoth ? ownPreview : m_AssetsPath /
                std::filesystem::path(EnsureAssetFileName(asset.fileName,
                    std::filesystem::path(targetFileName).stem().string() + ".png")).filename();
            destinations.push_back({path, &asset.imageBytes, true,
                !keepBoth && path.lexically_normal() == ownPreview.lexically_normal()});
        }
        Stack::Library::LibraryImportPlan plan;
        std::string error;
        // Recheck actual destinations when the user makes the choice. Only
        // Replace grants replacement of this project's own preview; imported
        // loose images and Keep Both destinations never gain that permission.
        if (!Stack::Library::PrepareLibraryImportPlan(
                {{targetProject, !keepBoth || conflict.pendingCopySaved}}, destinations, plan, error))
            return {ConflictResolutionState::Failed, std::move(error)};

        if (keepBoth && conflict.pendingCopySaved && std::filesystem::exists(targetProject)) {
            StackFormat::ProjectDocument savedCopy;
            StackFormat::ProjectLoadOptions fullLoad {true, true, true};
            if (!LoadProjectDocument(targetFileName, savedCopy, fullLoad) ||
                !StackFormat::AreProjectsIdentical(savedCopy, doc))
                return {ConflictResolutionState::Failed,
                    "The imported copy changed while waiting. Abort this import and start again."};
            resolved = true;
        } else if (StackFormat::WriteProjectFile(targetProject, doc, keepBoth)) {
            if (keepBoth) m_PendingConflicts[static_cast<std::size_t>(index)].pendingCopySaved = true;
            resolved = true;
        }
        if (resolved) {
            for (const auto& asset : plan.assets) {
                if (asset.reuse) continue;
                if (!Stack::FileSave::WriteExportFileAtomically(asset.target,
                        *destinations[asset.index].bytes, error))
                    return {ConflictResolutionState::Failed,
                        "The project was saved, but its Library image could not be copied: " +
                        error + " Existing unrelated images were kept. Retry or abort this import."};
            }
        }
    }
    } catch (const std::exception& error) {
        return { ConflictResolutionState::Failed, std::string("Could not save the imported project. ") + error.what() };
    } catch (...) {
        return { ConflictResolutionState::Failed, "Could not save the imported project. Check the destination and try again." };
    }

    if (!resolved) {
        return { ConflictResolutionState::Failed, "Could not save the imported project. Check that the Library folder is writable, then try again." };
    }

    if (resolved) {
        if (conflict.localPreviewTex) m_DeferredTextureDeletions.push_back(conflict.localPreviewTex);
        if (conflict.importedPreviewTex) m_DeferredTextureDeletions.push_back(conflict.importedPreviewTex);
        m_PendingConflicts.erase(m_PendingConflicts.begin() + index);

        if (m_PendingConflicts.empty()) {
            m_ImportStatusText = "Conflict resolution completed.";
            m_LastLibrarySignature = 0;
            RequestRefreshLibraryAsync();
            m_Notifier.CompleteActivity(m_ImportActivity, "Import completed.");
        }
    }
    return { ConflictResolutionState::Resolved, {} };
}

void LibraryManager::RequestImportLibraryBundle(const std::string& sourcePath) {
    if (sourcePath.empty() || Async::IsBusy(m_ImportTaskState) || HasPendingConflicts()) return;

    ++m_ImportGeneration;
    const std::uint64_t generation = m_ImportGeneration;
    m_ImportTaskState = Async::TaskState::Running;
    m_ImportStatusText = "Importing the library bundle in the background...";
    m_ImportActivity = m_Notifier.BeginActivity("Importing library");
    const auto activity = m_ImportActivity;
    const auto notifier = m_Notifier;

    const bool submitted = Async::TaskSystem::Get().Submit(LibraryActivity(notifier, activity, "Importing"),[this, generation, sourcePath, activity, notifier]() {
        std::string importError;
        const bool success = ImportLibraryBundle(sourcePath, &importError);
        Async::TaskSystem::Get().PostToMain([this, generation, success, activity, notifier, importError = std::move(importError)]() {
            if (generation != m_ImportGeneration) {
                return;
            }

            if (success) {
                m_ImportTaskState = Async::TaskState::Idle;
                m_ImportStatusText = "Library import completed.";
                if (!HasPendingConflicts()) {
                    notifier.CompleteActivity(activity, "Library imported.");
                } else {
                    m_ImportStatusText = "Choose how to handle the imported projects.";
                    notifier.UpdateActivity(activity, "Waiting for your choice");
                }
                m_LastLibrarySignature = 0;
                RequestRefreshLibraryAsync();
            } else {
                m_ImportTaskState = Async::TaskState::Failed;
                m_ImportStatusText = importError.empty() ? "Failed to import the library bundle." : importError;
                notifier.FailActivity(activity, "Could not import the library.", m_ImportStatusText);
            }
        });
    });
    if (!submitted) {
        m_ImportTaskState = Async::TaskState::Failed;
        m_ImportStatusText = "The library import could not be started.";
        notifier.FailActivity(activity, m_ImportStatusText);
    }
}

bool LibraryManager::WriteLibraryBundle(const Stack::FileSave::ExportFileTarget& target,
    std::string& error) {
    if (target.approval.path.empty()) return false;

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

        return StackFormat::WriteLibraryBundle(target, bundle, error);
    } catch (...) {
        error = "Could not collect files for the Library archive.";
        return false;
    }
}

bool LibraryManager::ImportLibraryBundle(const std::string& sourcePath, std::string* errorMessage) {
    if (errorMessage) errorMessage->clear();
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
            return FinalizeImport(bundle, {}, errorMessage);
        } else {
            // Import unrelated entries now; conflicted project entries wait for a decision.
            std::vector<int> skipped;
            for (const auto& conflict : conflicts) skipped.push_back(conflict.importedProjectIndex);
            if (!FinalizeImport(bundle, skipped, errorMessage)) return false;
            Async::TaskSystem::Get().PostToMain([this, bundle = std::move(bundle), conflicts = std::move(conflicts)]() mutable {
                ClearConflicts();
                m_ActiveImportBundle = std::move(bundle);
                for (auto& conflict : conflicts) conflict.id = m_NextConflictId++;
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

bool LibraryManager::FinalizeImport(const StackFormat::LibraryBundleDocument& bundle,
    const std::vector<int>& skippedProjectIndices, std::string* errorMessage) {
    const auto skipped = [&](std::size_t index) {
        return std::find(skippedProjectIndices.begin(), skippedProjectIndices.end(),
            static_cast<int>(index)) != skippedProjectIndices.end();
    };
    std::vector<Stack::Library::ImportProjectDestination> projects;
    for (std::size_t i = 0; i < bundle.projects.size(); ++i) {
        const auto& project = bundle.projects[i];
        projects.push_back({m_LibraryPath / EnsureProjectFileNameForKind(project.fileName,
            SanitizeFileStem(project.project.metadata.projectName), project.project.metadata.projectKind), skipped(i)});
    }
    std::vector<Stack::Library::ImportAssetDestination> assets;
    for (const auto& asset : bundle.assets) {
        const std::string fallback = std::filesystem::path(asset.projectFileName.empty()
            ? "imported_asset" : asset.projectFileName).stem().string() + ".png";
        Stack::Library::ImportAssetDestination destination;
        destination.path = m_AssetsPath /
            std::filesystem::path(EnsureAssetFileName(asset.fileName, fallback)).filename();
        destination.bytes = &asset.imageBytes;
        for (std::size_t i = 0; i < bundle.projects.size(); ++i) {
            if (!skipped(i) || bundle.projects[i].fileName != asset.projectFileName) continue;
            destination.deferred = true;
            // A project Replace decision may replace only its own preview,
            // never an unrelated loose image named by a bundle entry.
            destination.projectPreviewReplacement = destination.path.lexically_normal() ==
                BuildAssetPathForProjectFile(projects[i].path.filename().string()).lexically_normal();
            break;
        }
        assets.push_back(std::move(destination));
    }
    Stack::Library::LibraryImportPlan plan;
    std::string error;
    if (!Stack::Library::PrepareLibraryImportPlan(projects, assets, plan, error)) {
        if (errorMessage) *errorMessage = std::move(error);
        return false;
    }

    // Collision decisions above are read-only. Failures from this point can
    // leave successfully imported entries; do not describe a bundle transaction.
    const auto writeFailure = [&](const std::filesystem::path& path, const std::string& reason) {
        if (errorMessage) *errorMessage = "Import could not finish writing " + path.filename().u8string() +
            ": " + reason + " Earlier successfully imported entries remain in the Library.";
        return false;
    };
    std::error_code ec;
    std::filesystem::create_directories(m_LibraryPath, ec);
    if (ec) return writeFailure(m_LibraryPath, ec.message());
    std::filesystem::create_directories(m_AssetsPath, ec);
    if (ec) return writeFailure(m_AssetsPath, ec.message());
    for (std::size_t i = 0; i < projects.size(); ++i) {
        if (projects[i].deferred) continue;
        // A new project created since preflight is not implicit replacement
        // permission. Existing projects still use the conflict decision flow.
        if (!StackFormat::WriteProjectFile(projects[i].path, bundle.projects[i].project, true))
            return writeFailure(projects[i].path, "The new project destination could not be claimed.");
    }
    for (const auto& asset : plan.assets) {
        if (asset.deferred || asset.reuse) continue;
        if (!Stack::FileSave::WriteExportFileAtomically(asset.target,
                bundle.assets[asset.index].imageBytes, error))
            return writeFailure(asset.target.approval.path, error);
    }
    return true;
}

void LibraryManager::RequestImportAndLoad(
    const std::string& sourcePath,
    EditorModule* editor,
    CompositeModule* composite,
    std::function<void(int)> onTabSwitchRequested) {
    (void)composite;
    if (sourcePath.empty()) return;
    if (Async::IsBusy(m_ImportTaskState) || HasPendingConflicts()) {
        m_Notifier.Info("Finish the current import before opening another file.");
        return;
    }

    const std::filesystem::path path(sourcePath);
    if (!std::filesystem::exists(path)) return;

    if (path.extension() == ".stacklib") {
        RequestImportLibraryBundle(sourcePath);
        return;
    }

    if (IsSupportedProjectExtension(path)) {
        StackFormat::ProjectDocument document;
        StackFormat::ProjectLoadOptions metadataOnly { true, false, false };
        const bool success = StackFormat::ReadProjectFile(
            path, document, metadataOnly);

        if (!success) {
            m_ImportStatusText = "Failed to read project file metadata.";
            m_ImportTaskState = Async::TaskState::Failed;
            PostNotification(UiNotificationSeverity::Error, "Failed to read project file metadata.", "library-import-project");
            return;
        }

        if (document.metadata.projectKind == StackFormat::kCompositeProjectKind) {
            m_ImportStatusText = "Legacy standalone composite projects are no longer supported.";
            m_ImportTaskState = Async::TaskState::Failed;
            PostNotification(UiNotificationSeverity::Error, "Legacy standalone composite projects are no longer supported.", "library-import-project");
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
                m_Notifier.Error("Could not read the project to import.");
                return;
            }

            StackFormat::LibraryBundleDocument bundle;
            bundle.timestamp = BuildTimestampString();
            StackFormat::BundledProjectDocument bundled;
            bundled.fileName = fileName;
            bundled.project = std::move(document);
            bundle.projects.push_back(std::move(bundled));

            ClearConflicts();
            m_ActiveImportBundle = std::move(bundle);
            conflict.id = m_NextConflictId++;
            m_PendingConflicts.push_back(std::move(conflict));
            m_ImportActivity = m_Notifier.BeginActivity("Importing project");
            m_Notifier.UpdateActivity(m_ImportActivity, "Waiting for your choice");
            m_ImportTaskState = Async::TaskState::Idle;
            return;
        }

        std::error_code ec;
        std::filesystem::copy_file(path, m_LibraryPath / fileName, std::filesystem::copy_options::overwrite_existing, ec);
        if (ec) {
            m_ImportStatusText = "Failed to copy project to library: " + ec.message();
            m_ImportTaskState = Async::TaskState::Failed;
            PostNotification(UiNotificationSeverity::Error, m_ImportStatusText, "library-import-project");
            return;
        }

        m_LastLibrarySignature = 0;
        RequestRefreshLibraryAsync();

        if (document.metadata.projectKind == StackFormat::kRenderProjectKind) {
            m_ImportStatusText = "Render projects are no longer supported.";
            m_ImportTaskState = Async::TaskState::Failed;
            PostNotification(UiNotificationSeverity::Error, "Render projects are no longer supported.", "library-import-project");
            return;
        } else {
            if (document.metadata.projectKind == StackFormat::kCompositeProjectKind) {
                m_ImportStatusText = "Legacy standalone composite projects are no longer supported.";
                m_ImportTaskState = Async::TaskState::Failed;
                PostNotification(UiNotificationSeverity::Error, "Legacy standalone composite projects are no longer supported.", "library-import-project");
                return;
            }
            if (onTabSwitchRequested) onTabSwitchRequested(1);
            RequestLoadProjectFromPath(m_LibraryPath / fileName, editor);
        }
        return;
    }

    if (IsSupportedAssetExtension(path)) {
        const std::string fileName = path.filename().u8string();
        std::filesystem::path published;
        std::string error;
        bool imported = false;
        try {
            std::vector<unsigned char> bytes;
            if (!ReadFileBytes(path, bytes)) error = "The dropped image could not be read.";
            else imported = Stack::Library::WriteNewLibraryAsset(m_AssetsPath, std::filesystem::u8path(fileName), bytes, published, error);
        } catch (const std::exception& exception) {
            error = exception.what();
        }
        if (!imported) {
            m_ImportStatusText = "Failed to import image to Library: " + error;
            m_ImportTaskState = Async::TaskState::Failed;
            PostNotification(UiNotificationSeverity::Error, m_ImportStatusText, "library-import-asset");
            return;
        }

        m_LastLibrarySignature = 0;
        RequestRefreshLibraryAsync();
        if (published.filename() != std::filesystem::u8path(fileName)) {
            PostNotification(UiNotificationSeverity::Info,
                "Existing image kept. Imported as " + published.filename().u8string(), "library-import-asset");
        }
        if (editor) {
            if (onTabSwitchRequested) onTabSwitchRequested(1);
            editor->RequestLoadSourceImage(published.string());
        }
        return;
    }

    m_ImportStatusText = "Unsupported file type dropped.";
    m_ImportTaskState = Async::TaskState::Failed;
    PostNotification(UiNotificationSeverity::Error, "Unsupported file type dropped.", "library-import-asset");
}
