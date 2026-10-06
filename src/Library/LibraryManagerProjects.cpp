#include "LibraryManager.h"

#include "Async/TaskSystem.h"
#include "Editor/EditorModule.h"
#include "Library/Internal/LibraryImageHelpers.h"
#include "Library/Internal/LibraryStorageHelpers.h"
#include "Library/TagManager.h"
#include "Persistence/ProjectIndex.h"
#include "Persistence/ProjectOpenCoordinator.h"
#include "Persistence/ProjectSaveCapture.h"
#include "Utils/PixelBufferUtils.h"

#include <cstddef>
#include <cstdint>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <stdexcept>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "Utils/PngEncodingUtils.h"
#undef STB_IMAGE_WRITE_IMPLEMENTATION

namespace {

namespace StackFormat = StackBinaryFormat;
namespace LibraryImage = Stack::Library::ImageHelpers;

} // namespace

using namespace Stack::Library::StorageHelpers;

Async::TaskState LibraryManager::GetSaveTaskState(const EditorModule* editor) const {
    return editor ? editor->GetProjectFileOperations()->save.state : m_SaveTaskState;
}

const std::string& LibraryManager::GetSaveStatusText(const EditorModule* editor) const {
    return editor ? editor->GetProjectFileSaveStatusText() : m_SaveStatusText;
}

Async::TaskState LibraryManager::GetProjectLoadTaskState(const EditorModule* editor) const {
    return editor ? editor->GetProjectLoadTaskState() : m_ProjectLoadTaskState;
}

const std::string& LibraryManager::GetProjectLoadStatusText(const EditorModule* editor) const {
    return editor ? editor->GetProjectLoadStatusText() : m_ProjectLoadStatusText;
}

bool LibraryManager::LoadProjectDocument(
    const std::string& fileName,
    StackFormat::ProjectDocument& outDocument,
    const StackFormat::ProjectLoadOptions& options) {

    const std::filesystem::path path = ResolveProjectPath(fileName);
    if (!std::filesystem::exists(path)) return false;

    std::lock_guard<std::mutex> fileLock(m_ProjectFileIoMutex);
    return StackFormat::ReadProjectFile(path, outDocument, options);
}

std::filesystem::path LibraryManager::ResolveProjectPath(
    const std::string& projectKey) const {
    if (projectKey.empty()) return {};
    const std::filesystem::path direct(projectKey);
    if (direct.is_absolute()) return direct.lexically_normal();
    std::lock_guard<std::mutex> projectLock(m_ProjectsMutex);
    for (const std::shared_ptr<ProjectEntry>& project : m_Projects) {
        if (project && project->fileName == projectKey &&
            !project->absolutePath.empty()) {
            return project->absolutePath.lexically_normal();
        }
    }
    return (m_LibraryPath / direct).lexically_normal();
}

std::uint64_t LibraryManager::BumpNodeBrowserThumbnailPersistRevision(
    const std::filesystem::path& projectPath) {
    const std::filesystem::path key = projectPath.lexically_normal();
    std::lock_guard<std::mutex> lock(m_NodeBrowserThumbnailPersistMutex);
    return ++m_NodeBrowserThumbnailPersistRevisions[key];
}

bool LibraryManager::IsNodeBrowserThumbnailPersistRevisionCurrent(
    const std::filesystem::path& projectPath,
    std::uint64_t revision) const {
    if (revision == 0) {
        return false;
    }
    const std::filesystem::path key = projectPath.lexically_normal();
    std::lock_guard<std::mutex> lock(m_NodeBrowserThumbnailPersistMutex);
    const auto it = m_NodeBrowserThumbnailPersistRevisions.find(key);
    return it != m_NodeBrowserThumbnailPersistRevisions.end() &&
        it->second == revision;
}

bool LibraryManager::OverwriteEditorProject(
    const std::string& fileName,
    const std::string& projectName,
    const std::vector<unsigned char>& sourcePngBytes,
    const StackFormat::json& pipelineData,
    const std::vector<unsigned char>& renderedPixels,
    const int renderedW,
    const int renderedH) {

    if (fileName.empty() ||
        sourcePngBytes.empty() ||
        renderedPixels.empty() ||
        renderedW <= 0 ||
        renderedH <= 0) {
        return false;
    }

    const std::string trimmedName = TrimWhitespace(projectName).empty() ? "Untitled Project" : TrimWhitespace(projectName);
    const std::string resolvedFileName = EnsureProjectFileName(fileName, SanitizeFileStem(trimmedName) + ".stack");
    const std::filesystem::path projectPath = m_LibraryPath / resolvedFileName;
    const std::filesystem::path assetPath = BuildAssetPathForProjectFile(resolvedFileName);

    int sourceW = 0;
    int sourceH = 0;
    int sourceChannels = 0;
    std::vector<unsigned char> decodedSourcePixels;
    if (!DecodeImageBytes(sourcePngBytes, decodedSourcePixels, sourceW, sourceH, sourceChannels)) {
        return false;
    }

    StackFormat::ProjectDocument document;
    document.metadata.projectKind = StackFormat::kEditorProjectKind;
    document.metadata.projectName = trimmedName;
    document.metadata.timestamp = BuildTimestampString();
    document.metadata.sourceWidth = sourceW;
    document.metadata.sourceHeight = sourceH;
    document.thumbnailBytes = GenerateThumbnailBytes(renderedPixels, renderedW, renderedH);
    document.sourceImageBytes = sourcePngBytes;
    document.pipelineData = pipelineData;

    std::vector<unsigned char> renderedPngBytes =
        Stack::PngEncoding::EncodeInterleaved(
            renderedPixels, renderedW, renderedH, 4);
    if (renderedPngBytes.empty()) {
        return false;
    }

    try {
        if (!StackFormat::WriteProjectFile(projectPath, document)) {
            return false;
        }
        if (!WriteFileBytes(assetPath, renderedPngBytes)) {
            return false;
        }
    } catch (...) {
        return false;
    }

    m_LastLibrarySignature = 0;
    return true;
}

void LibraryManager::RequestSaveProject(
    const std::string& name,
    EditorModule* editor,
    const std::string& existingFileName,
    std::function<void(bool)> onComplete,
    bool requireNewStore) {
    if (!editor) {
        if (onComplete) onComplete(false);
        return;
    }
    const auto operations = editor->GetProjectFileOperations();
    try {
        RequestSaveProjectImpl(
            name,
            editor,
            existingFileName,
            {},
            onComplete,
            requireNewStore);
    } catch (...) {
        operations->save.state = Async::TaskState::Failed;
        try {
            operations->save.statusText = "Failed to prepare the project save.";
            editor->ShowUiNotification(
                UiNotificationSeverity::Error,
                operations->save.statusText,
                "library-save-project");
        } catch (...) {
            operations->save.statusText.clear();
        }
        if (onComplete) {
            try {
                onComplete(false);
            } catch (...) {
            }
        }
    }
}

void LibraryManager::RequestSaveProjectToPath(
    const std::string& name,
    EditorModule* editor,
    const std::filesystem::path& absoluteDestination,
    std::function<void(bool)> onComplete,
    bool requireNewStore) {
    if (!editor) {
        if (onComplete) onComplete(false);
        return;
    }
    const auto operations = editor->GetProjectFileOperations();
    try {
        if (absoluteDestination.empty() || !absoluteDestination.is_absolute()) {
            editor->ShowUiNotification(
                UiNotificationSeverity::Error,
                "The project save destination must be an absolute path.",
                "library-save-project-path");
            if (onComplete) onComplete(false);
            return;
        }
        RequestSaveProjectImpl(
            name,
            editor,
            {},
            absoluteDestination.lexically_normal(),
            std::move(onComplete),
            requireNewStore);
    } catch (...) {
        operations->save.state = Async::TaskState::Failed;
        operations->save.statusText = "Failed to prepare the project save.";
        editor->ShowUiNotification(
            UiNotificationSeverity::Error,
            operations->save.statusText,
            "library-save-project-path");
        if (onComplete) onComplete(false);
    }
}

void LibraryManager::RequestSaveProjectImpl(
    const std::string& name,
    EditorModule* editor,
    const std::string& existingFileName,
    const std::filesystem::path& absoluteDestination,
    std::function<void(bool)> onComplete,
    bool requireNewStore) {
    if (!editor) {
        if (onComplete) onComplete(false);
        return;
    }
    const auto operations = editor->GetProjectFileOperations();
    const std::weak_ptr<Stack::Project::FileOperationState> owner = operations;
    if (Async::IsBusy(operations->save.state)) {
        editor->ShowUiNotification(UiNotificationSeverity::Error, "Failed to save the project to the library.", "library-save-project");
        if (onComplete) onComplete(false);
        return;
    }

    operations->save.state = Async::TaskState::Applying;
    operations->save.statusText = "Capturing the project snapshot for the library...";

    const std::string trimmedName = TrimWhitespace(name).empty() ? "Untitled Project" : TrimWhitespace(name);
    StackFormat::json pipeline = editor->SerializePipeline();
    std::vector<StackFormat::NodeBrowserThumbnailEntry> nodeBrowserThumbnailEntries =
        editor->GetPersistedNodeBrowserThumbnails();
    const std::uint64_t capturedEditRevision =
        editor->GetProjectEditRevision();
    const std::string capturedProjectId = editor->EnsureProjectDocumentId();
    const std::filesystem::path capturedAdoptionSource =
        editor->GetProjectAdoptionSourcePath();

    int renderedW = 0;
    int renderedH = 0;
    std::vector<unsigned char> renderedPixels;
    int sourceW = 0;
    int sourceH = 0;
    std::vector<unsigned char> sourcePixels;
    std::vector<unsigned char> sourcePngBytesOverride;

    // Saving captures existing state only. It must never initiate a render or
    // require a cover; a missing/stale cover is rebuilt by the Library later.
    renderedPixels = editor->GetPipeline().GetOutputPixels(renderedW, renderedH);
    sourcePixels = editor->GetPipeline().GetSourcePixels(sourceW, sourceH);
    std::vector<unsigned char> graphSourcePngBytes;
    if (LibraryImage::ExtractEmbeddedGraphSourcePng(
            pipeline,
            graphSourcePngBytes)) {
        std::vector<unsigned char> decodedGraphSourcePixels;
        int graphSourceW = 0;
        int graphSourceH = 0;
        int graphSourceChannels = 0;
        if (DecodeImageBytes(
                graphSourcePngBytes,
                decodedGraphSourcePixels,
                graphSourceW,
                graphSourceH,
                graphSourceChannels) &&
            !decodedGraphSourcePixels.empty() &&
            graphSourceW > 0 && graphSourceH > 0) {
            sourcePngBytesOverride = std::move(graphSourcePngBytes);
            sourcePixels = std::move(decodedGraphSourcePixels);
            sourceW = graphSourceW;
            sourceH = graphSourceH;
        }
    }

    if (renderedPixels.empty() || renderedW <= 0 || renderedH <= 0) {
        // Project state is authoritative even before the graph has a usable
        // output. A transparent placeholder keeps the container valid while
        // preserving the graph exactly as authored.
        renderedW = 1;
        renderedH = 1;
        renderedPixels =
            Stack::PixelBuffer::BuildTransparentRgbaPixels(renderedW, renderedH);
    }

    if (sourcePixels.empty() || sourceW <= 0 || sourceH <= 0) {
        sourceW = renderedW;
        sourceH = renderedH;
        sourcePixels =
            Stack::PixelBuffer::BuildTransparentRgbaPixels(sourceW, sourceH);
    }

    std::filesystem::path projectPath = absoluteDestination;
    std::string fileName = existingFileName;
    if (projectPath.empty() && !fileName.empty() &&
        std::filesystem::path(fileName).is_absolute()) {
        projectPath = std::filesystem::path(fileName).lexically_normal();
        fileName.clear();
    }
    if (fileName.empty() && editor && !editor->GetCurrentProjectFileName().empty()) {
        const std::filesystem::path currentPath(editor->GetCurrentProjectFileName());
        if (projectPath.empty() && currentPath.is_absolute()) {
            projectPath = currentPath.lexically_normal();
        } else if (projectPath.empty()) {
            fileName = editor->GetCurrentProjectFileName();
        }
    }
    if (projectPath.empty() && fileName.empty()) {
        projectPath = Stack::Project::ProjectIndex::BuildUniqueProjectPath(
            m_LibraryPath,
            trimmedName,
            capturedProjectId);
    }
    if (projectPath.empty()) {
        projectPath = (m_LibraryPath / fileName).lexically_normal();
    } else {
        fileName = projectPath.string();
    }
    const bool publishLibraryArtifacts =
        projectPath.parent_path().lexically_normal() ==
        m_LibraryPath.lexically_normal();
    if (publishLibraryArtifacts && !m_ProjectRootWritable) {
        operations->save.state = Async::TaskState::Failed;
        operations->save.statusText = m_ProjectRootWriteError.empty()
            ? "Stack Projects is read-only. Use Save As to choose a writable location."
            : m_ProjectRootWriteError;
        editor->ShowUiNotification(
            UiNotificationSeverity::Error,
            operations->save.statusText,
            "library-project-root-read-only");
        if (onComplete) onComplete(false);
        return;
    }
    const std::string assetFileName =
        BuildAssetPathForProjectFile(projectPath.filename().string())
            .filename()
            .string();

    Stack::Project::ProjectSaveCapture capture;
    capture.document.projectId = capturedProjectId;
    capture.document.adoptedFrom = capturedAdoptionSource;
    capture.document.metadata.projectKind = StackFormat::kEditorProjectKind;
    capture.document.metadata.projectName = trimmedName;
    capture.document.metadata.timestamp = BuildTimestampString();
    capture.document.metadata.sourceWidth = sourceW;
    capture.document.metadata.sourceHeight = sourceH;
    capture.document.sourceImageBytes = std::move(sourcePngBytesOverride);
    capture.document.pipelineData = std::move(pipeline);
    capture.document.nodeBrowserThumbnailEntries = std::move(nodeBrowserThumbnailEntries);
    capture.sourcePixels = std::move(sourcePixels);
    capture.renderedPixels = std::move(renderedPixels);
    capture.renderedWidth = renderedW;
    capture.renderedHeight = renderedH;
    capture.includeLibraryPreview = publishLibraryArtifacts;
    const std::string capturedTimestamp = capture.document.metadata.timestamp;

    const std::uint64_t generation = operations->save.Begin(
        capturedProjectId, capturedEditRevision);
    operations->save.state = Async::TaskState::Running;
    operations->save.statusText = "Packaging and writing project files in the background...";

    bool submitted = false;
    try {
        submitted = editor->ProjectTasks().Submit("Saving",[this, owner,
                                         generation,
                                         trimmedName,
                                         fileName,
                                         projectPath,
                                         publishLibraryArtifacts,
                                         assetFileName,
                                         capture = std::move(capture),
                                         renderedW,
                                         renderedH,
                                         capturedTimestamp,
                                         capturedEditRevision,
                                         capturedProjectId,
                                         requireNewStore,
                                         editor,
                                         onComplete]() mutable {
        bool wroteProject = false;
        bool wroteAsset = false;
        std::string projectError;
        std::string previewWarning;
        auto saved = std::make_shared<Stack::Project::CapturedProjectSaveResult>();

        try {
            *saved = Stack::Project::WriteCapturedProject(projectPath, std::move(capture), requireNewStore);
            wroteProject = static_cast<bool>(saved->project);
            if (!wroteProject) {
                projectError = saved->project.commit.message;
            }
            if (wroteProject && publishLibraryArtifacts) {
                const auto& renderedPngBytes = saved->libraryPreviewBytes;
                wroteAsset = !renderedPngBytes.empty() &&
                    WriteFileBytes(
                        m_AssetsPath / assetFileName,
                        renderedPngBytes);
                if (!wroteAsset) {
                    previewWarning = !saved->previewWarning.empty()
                        ? saved->previewWarning
                        : "the preview image could not be written";
                }

                if (wroteAsset) {
                    std::string hash = ComputeImageHash(renderedPngBytes);
                    nlohmann::json meta = {
                        {"hash", hash},
                        {"projectFileName", fileName},
                        {"displayName", trimmedName},
                        {"timestamp", capturedTimestamp},
                        {"width", renderedW},
                        {"height", renderedH}
                    };
                    std::ofstream f(m_AssetsPath / (assetFileName + ".hash"));
                    if (f.is_open()) {
                        f << meta.dump(4);
                    }
                }

            } else if (wroteProject) {
                // An externally located .stack file is self-contained. Do
                // not create or overwrite managed-Library sidecars merely
                // because that project is currently open in the Editor.
                wroteAsset = true;
            }
        } catch (const std::exception& exception) {
            if (wroteProject) {
                wroteAsset = false;
                previewWarning = exception.what();
            } else {
                projectError = exception.what();
            }
        } catch (...) {
            if (wroteProject) {
                wroteAsset = false;
                previewWarning = "an unknown error interrupted the rebuildable Library preview";
            } else {
                projectError = "an unknown error interrupted the authoritative project write";
            }
        }

        editor->ProjectTasks().PostToMain([
            this, owner, capturedProjectId,
            generation,
            wroteProject,
            wroteAsset,
            projectError = std::move(projectError),
            previewWarning = std::move(previewWarning),
            trimmedName,
            fileName,
            publishLibraryArtifacts,
            capturedEditRevision,
            saved,
            editor,
            onComplete = std::move(onComplete)
        ]() {
            const auto operations = owner.lock();
            if (!operations) return;
            if (generation != operations->save.generation) {
                if (onComplete) onComplete(false);
                return;
            }
            if (editor->GetProjectDocumentId() != capturedProjectId) {
                operations->save.Invalidate();
                if (onComplete) onComplete(false);
                return;
            }

            bool completionSuccess = false;
            try {
            if (wroteProject) {
                operations->save.state = Async::TaskState::Idle;
                if (publishLibraryArtifacts) {
                    m_LastLibrarySignature = 0;
                }
                bool savedSnapshotStillCurrent = true;
                if (editor) {
                    if (editor->GetProjectEditRevision() == capturedEditRevision) {
                        editor->SetCurrentProjectName(trimmedName);
                    }
                    editor->SetCurrentProjectFileName(fileName);
                    if (!editor->AdoptSavedProjectStore(saved->project.store,
                            std::move(saved->project.snapshot), capturedProjectId, capturedEditRevision)) {
                        throw std::runtime_error("The saved snapshot no longer belongs to this project.");
                    }
                    savedSnapshotStillCurrent =
                        editor->ClearDirtyIfRevision(capturedEditRevision);
                }
                if (savedSnapshotStillCurrent) {
                    if (publishLibraryArtifacts && !wroteAsset) {
                        operations->save.statusText =
                            "Project saved. The rebuildable Library preview was not updated" +
                            (previewWarning.empty()
                                ? std::string(".")
                                : std::string(": ") + previewWarning + ".");
                        Stack::Notifications::NoticeSpec warning;
                        warning.title = "Preview not updated";
                        warning.message = "The project was saved.";
                        warning.details = operations->save.statusText;
                        warning.context = trimmedName;
                        warning.severity = Stack::Notifications::Severity::Warning;
                        warning.outcome = Stack::Notifications::Outcome::Partial;
                        warning.dedupeKey = "library-save-preview-warning";
                        const auto work = Async::TaskSystem::CurrentActivity();
                        if (work.ownerId == editor->GetNotifier().GetOwner().id) warning.operationId = work.operationId;
                        editor->GetNotifier().Post(std::move(warning));
                    } else {
                        operations->save.statusText = publishLibraryArtifacts
                            ? "Project saved to the library."
                            : "Project saved.";
                        // The project save coordinator reports explicit saves.
                        // Autosaves and their low-level writer settle quietly.
                    }
                } else {
                    operations->save.statusText =
                        "Project snapshot saved; newer edits are still unsaved.";
                    // Keep the status for the owning save coordinator, which
                    // distinguishes explicit save feedback from autosave.
                }
                // The captured snapshot is durable even when newer edits are
                // present. The save coordinator owns the follow-up revision.
                completionSuccess = true;
            } else {
                operations->save.state = Async::TaskState::Failed;
                operations->save.statusText = publishLibraryArtifacts
                    ? "Failed to save the project to the library."
                    : "Failed to save the project.";
                if (!projectError.empty()) {
                    operations->save.statusText += " " + projectError;
                }
                editor->ShowUiNotification(
                    UiNotificationSeverity::Error,
                    operations->save.statusText,
                    "library-save-project");
            }
            } catch (const std::exception& error) {
                operations->save.state = Async::TaskState::Failed;
                operations->save.statusText =
                    "The project was written, but save finalization failed: " +
                    std::string(error.what());
            } catch (...) {
                operations->save.state = Async::TaskState::Failed;
                operations->save.statusText =
                    "The project save finalization failed unexpectedly.";
            }
            if (onComplete) onComplete(completionSuccess);
        });
        });
    } catch (...) {
        submitted = false;
    }
    if (!submitted && generation == operations->save.generation) {
        operations->save.state = Async::TaskState::Failed;
        operations->save.statusText = "The project save could not be queued.";
        editor->ShowUiNotification(
            UiNotificationSeverity::Error,
            operations->save.statusText,
            "library-save-project");
        if (onComplete) onComplete(false);
    }
}

void LibraryManager::RequestPersistNodeBrowserThumbnails(
    const std::string& fileName,
    std::vector<StackFormat::NodeBrowserThumbnailEntry> entries) {
    if (fileName.empty()) {
        return;
    }

    const std::filesystem::path projectPath = ResolveProjectPath(fileName);
    // Managed thumbnails are rebuildable runtime data. The legacy writer
    // cannot update their manifest field and must not rewrite the project.
    if (Stack::Project::IsDirectoryProjectBundle(projectPath) ||
        Stack::Project::IsPortableV3Project(projectPath)) return;
    std::uint64_t revision = 0;
    try {
        revision = BumpNodeBrowserThumbnailPersistRevision(projectPath);
    } catch (...) {
        return;
    }
    bool submitted = false;
    try {
        submitted = Async::TaskSystem::Get().Submit(MakeActivityMetadata("Saving previews", true),
            [this, projectPath, revision, entries = std::move(entries)]() mutable {
        bool success = false;
        try {
            std::lock_guard<std::mutex> fileLock(m_ProjectFileIoMutex);
            if (!std::filesystem::exists(projectPath) ||
                !IsNodeBrowserThumbnailPersistRevisionCurrent(
                    projectPath,
                    revision)) {
                return;
            }

            StackFormat::ProjectLoadOptions options;
            options.includeThumbnail = true;
            options.includeSourceImage = true;
            options.includePipelineData = true;
            options.includeNodeBrowserThumbnails = true;

            StackFormat::ProjectDocument document;
            success = StackFormat::ReadProjectFile(
                projectPath, document, options);
            if (!success) {
                return;
            }

            if (!IsNodeBrowserThumbnailPersistRevisionCurrent(
                    projectPath,
                    revision)) {
                return;
            }
            document.nodeBrowserThumbnailEntries = std::move(entries);
            success = StackFormat::WriteProjectFile(projectPath, document);
        } catch (...) {
            success = false;
        }

        if (success) {
            Async::TaskSystem::Get().PostToMain([this]() {
                m_LastLibrarySignature = 0;
            });
        }
            });
    } catch (...) {
        submitted = false;
    }
    (void)submitted;
}

void LibraryManager::RequestLoadProject(
    const std::string& fileName,
    EditorModule* editor,
    std::function<void(bool)> onComplete) {
    RequestLoadProjectFromPath(ResolveProjectPath(fileName), editor, std::move(onComplete));
}

void LibraryManager::RequestLoadProjectDeferredApply(
    const std::string& fileName,
    EditorModule* editor,
    std::function<void(bool, std::shared_ptr<Stack::Project::LoadedProjectData>)> onReady) {
    if (fileName.empty() || !editor) {
        if (onReady) onReady(false, nullptr);
        return;
    }
    if (editor->IsDeferredLoadedProjectApplyActive() ||
        Async::IsBusy(editor->GetProjectLoadTaskState())) {
        if (onReady) onReady(false, nullptr);
        return;
    }

    const auto operations = editor->GetProjectFileOperations();
    const std::weak_ptr<Stack::Project::FileOperationState> owner = operations;
    const auto generation = operations->load.Begin(
        editor->GetProjectDocumentId(), editor->GetProjectEditRevision());
    const std::string sourceDocumentId = operations->load.documentId;
    const auto sourceEditRevision = operations->load.editRevision;
    operations->load.statusText = "Loading the project in the background...";
    const auto projectPath = ResolveProjectPath(fileName);

    bool submitted = false;
    try {
        submitted = editor->ProjectTasks().Submit("Loading project",
            [editor, owner, generation, sourceDocumentId, sourceEditRevision,
             projectPath, onReady]() mutable {
                Stack::Project::ProjectOpenResult opened;
                try {
                    opened = Stack::Project::ProjectOpenCoordinator::Load(projectPath);
                } catch (const std::exception& exception) {
                    opened.error = exception.what();
                } catch (...) {
                    opened.error = "An unknown error interrupted the project load.";
                }
                editor->ProjectTasks().PostToMain(
                    [editor, owner, generation, sourceDocumentId, sourceEditRevision,
                     opened = std::move(opened), onReady = std::move(onReady)]() mutable {
                        const auto operations = owner.lock();
                        if (!operations) return;
                        if (generation != operations->load.generation) {
                            if (onReady) onReady(false, nullptr);
                            return;
                        }
                        if (editor->GetProjectDocumentId() != sourceDocumentId ||
                            editor->GetProjectEditRevision() != sourceEditRevision) {
                            operations->load.Invalidate();
                            if (onReady) onReady(false, nullptr);
                            return;
                        }
                        if (!opened) {
                            operations->load.state = Async::TaskState::Failed;
                            operations->load.statusText = opened.error.empty()
                                ? "Failed to load the selected project." : opened.error;
                            editor->ShowUiNotification(UiNotificationSeverity::Error,
                                operations->load.statusText, "project-load");
                            if (onReady) onReady(false, nullptr);
                            return;
                        }
                        operations->load.state = Async::TaskState::Applying;
                        operations->load.statusText = "Project data decoded.";
                        operations->load.warning = std::move(opened.warning);
                        if (onReady) onReady(true, std::move(opened.candidate));
                    });
            });
    } catch (...) {
        submitted = false;
    }
    if (!submitted && generation == operations->load.generation) {
        operations->load.state = Async::TaskState::Failed;
        operations->load.statusText = "The project load could not be queued.";
        editor->ShowUiNotification(UiNotificationSeverity::Error,
            operations->load.statusText, "project-load");
        if (onReady) onReady(false, nullptr);
    }
}

void LibraryManager::SetProjectLoadApplyingStatus(
    EditorModule* editor, const std::string& statusText) {
    if (!editor) return;
    auto& load = editor->GetProjectFileOperations()->load;
    load.state = Async::TaskState::Applying;
    load.statusText = statusText;
}

void LibraryManager::FinishDeferredProjectLoad(
    EditorModule* editor, bool success, const std::string& message) {
    if (!editor) return;
    auto& load = editor->GetProjectFileOperations()->load;
    load.state = success ? Async::TaskState::Idle : Async::TaskState::Failed;
    load.statusText = success && !load.warning.empty() ? load.warning
        : !message.empty() ? message
        : success ? "Project opened." : "Failed to apply the loaded project.";
    editor->ShowUiNotification(!success ? UiNotificationSeverity::Error
            : load.warning.empty() ? UiNotificationSeverity::Success
            : UiNotificationSeverity::Info,
        load.statusText, "project-load");
}

bool LibraryManager::RenameProject(const std::string& fileName, const std::string& newName) {
    const std::string trimmedName = TrimWhitespace(newName);
    if (trimmedName.empty()) return false;

    StackFormat::ProjectDocument document;
    if (!LoadProjectDocument(fileName, document, {})) return false;

    document.metadata.projectName = trimmedName;
    document.metadata.timestamp = BuildTimestampString();

    if (!StackFormat::WriteProjectFile(ResolveProjectPath(fileName), document)) {
        return false;
    }

    for (auto& project : m_Projects) {
        if (project && project->fileName == fileName) {
            project->projectName = trimmedName;
            project->timestamp = document.metadata.timestamp;
            break;
        }
    }

    for (auto& asset : m_Assets) {
        if (asset && asset->projectFileName == fileName) {
            asset->displayName = trimmedName;
            asset->projectName = trimmedName;
        }
    }

    m_LastLibrarySignature = 0;
    return true;
}

bool LibraryManager::DeleteProject(const std::string& fileName) {
    try {
        const std::filesystem::path projectPath = ResolveProjectPath(fileName);
        if (!std::filesystem::exists(projectPath)) return false;

        // Library removal is only available for a project that passes the
        // current strict store/schema check. Obsolete folders are deliberately
        // left on disk and are not surfaced by the rebuilt index.
        Stack::Project::ProjectStoreOpenResult currentProject =
            Stack::Project::OpenProjectStore(projectPath);
        if (!currentProject) return false;

        StackFormat::ProjectDocument document;
        StackFormat::ProjectLoadOptions metadataOnly { true, false, false };
        const bool loadedMetadata = LoadProjectDocument(fileName, document, metadataOnly);
        std::error_code removeError;
        const bool removedProject = Stack::Project::IsDirectoryProjectBundle(projectPath)
            ? std::filesystem::remove_all(projectPath, removeError) > 0u
            : false;
        if (!removedProject || removeError) return false;
        TagManager::Get().SetTags(fileName, {});
        if (!loadedMetadata || document.metadata.projectKind != StackFormat::kCompositeProjectKind) {
            const std::filesystem::path assetPath = BuildAssetPathForProjectFile(fileName);
            if (std::filesystem::exists(assetPath)) {
                std::error_code ec;
                std::filesystem::remove(assetPath, ec);
                if (ec) {
                    m_Notifier.Warning("The project was deleted, but its preview could not be removed.",
                        "Preview cleanup", assetPath.string() + "\n" + ec.message());
                } else {
                    std::filesystem::remove(assetPath.string() + ".hash", ec);
                    TagManager::Get().SetTags(assetPath.filename().string(), {});
                }
            }
        }

        m_LastLibrarySignature = 0;
        return true;
    } catch (...) {
        return false;
    }
}

std::filesystem::path LibraryManager::BuildAssetPathForProjectFile(const std::string& projectFileName) const {
    return m_AssetsPath / (std::filesystem::path(projectFileName).stem().string() + ".png");
}

void LibraryManager::RequestLoadProjectFromPath(
    const std::filesystem::path& absolutePath,
    EditorModule* editor,
    std::function<void(bool)> onComplete) {
    if (!editor || absolutePath.empty()) {
        if (onComplete) onComplete(false);
        return;
    }
    if (editor->IsDeferredLoadedProjectApplyActive() ||
        Async::IsBusy(editor->GetProjectLoadTaskState())) {
        if (onComplete) onComplete(false);
        return;
    }
    const std::weak_ptr<Stack::Project::FileOperationState> owner =
        editor->GetProjectFileOperations();
    const std::string sourceDocumentId = editor->GetProjectDocumentId();
    const auto waitingLoadGeneration = editor->GetProjectFileOperations()->load.generation;
    if (editor->RequestAutoBracketForeground("open this project",
            [this, absolutePath, editor, owner, sourceDocumentId, onComplete] {
                if (owner.expired()) return;
                if (editor->GetProjectDocumentId() != sourceDocumentId) {
                    if (onComplete) onComplete(false);
                    return;
                }
                RequestLoadProjectFromPath(absolutePath, editor, onComplete);
            })) return;
    if (!editor->FinishWorkspaceInteraction()) {
        if (onComplete) onComplete(false);
        return;
    }
    if (editor->NeedsWorkspaceSaveBeforeTransition() || editor->IsProjectFileSaveBusy() ||
        editor->IsRawWorkspaceProjectSaveBusy()) {
        editor->RequestSaveWorkspaceBeforeClose(
            [this, absolutePath, editor, owner, waitingLoadGeneration, onComplete](bool success) {
                const auto operations = owner.lock();
                if (!operations) return;
                if (success && operations->load.generation == waitingLoadGeneration) {
                    RequestLoadProjectFromPath(absolutePath, editor, onComplete);
                } else {
                    editor->ShowUiNotification(UiNotificationSeverity::Info,
                        "Could not save the current project. It will stay open.", "project-load-save-failed");
                    if (onComplete) onComplete(false);
                }
            });
        return;
    }
    RequestLoadProjectDeferredApply(absolutePath.string(), editor,
        [this, editor, owner, onComplete](bool loaded,
            std::shared_ptr<Stack::Project::LoadedProjectData> project) mutable {
            const auto operations = owner.lock();
            if (!operations) return;
            if (!loaded || !project) {
                if (onComplete) onComplete(false);
                return;
            }
            const auto generation = operations->load.generation;
            const std::string loadedProjectId = project->projectId;
            operations->load.statusText = "Applying project data...";
            try {
                const bool started = editor->BeginDeferredLoadedProjectApply(project,
                    [this, editor, owner, generation, loadedProjectId, onComplete](
                        bool applied, const std::string& status) mutable {
                        const auto operations = owner.lock();
                        if (!operations) return;
                        if (generation != operations->load.generation ||
                            (applied && !loadedProjectId.empty() &&
                             editor->GetProjectDocumentId() != loadedProjectId)) {
                            if (onComplete) onComplete(false);
                            return;
                        }
                        FinishDeferredProjectLoad(editor, applied, status);
                        if (onComplete) onComplete(applied);
                    });
                if (started) return;
                FinishDeferredProjectLoad(editor, false, editor->GetDeferredLoadedProjectStatusText());
            } catch (const std::exception& exception) {
                FinishDeferredProjectLoad(editor, false, exception.what());
            } catch (...) {
                FinishDeferredProjectLoad(editor, false, "Failed to apply project data.");
            }
            if (onComplete) onComplete(false);
        });
}
