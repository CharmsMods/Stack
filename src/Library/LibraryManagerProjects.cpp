#include "LibraryManager.h"

#include "Async/TaskSystem.h"
#include "Editor/EditorModule.h"
#include "Library/Internal/LibraryImageHelpers.h"
#include "Library/Internal/LibraryStorageHelpers.h"
#include "Library/TagManager.h"
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

bool LibraryManager::LoadProjectDocument(
    const std::string& fileName,
    StackFormat::ProjectDocument& outDocument,
    const StackFormat::ProjectLoadOptions& options) {

    const std::filesystem::path path = m_LibraryPath / fileName;
    if (!std::filesystem::exists(path)) return false;

    std::lock_guard<std::mutex> fileLock(m_ProjectFileIoMutex);
    if (StackFormat::ReadProjectFile(path, outDocument, options)) {
        return true;
    }

    return LoadLegacyProjectDocument(path, outDocument, options);
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
    std::function<void(bool)> onComplete) {
    try {
        RequestSaveProjectImpl(
            name,
            editor,
            existingFileName,
            {},
            onComplete);
    } catch (...) {
        m_SaveTaskState = Async::TaskState::Failed;
        try {
            m_SaveStatusText = "Failed to prepare the project save.";
            QueueUiNotification(
                UiNotificationSeverity::Error,
                m_SaveStatusText,
                "library-save-project");
        } catch (...) {
            m_SaveStatusText.clear();
        }
        if (editor) {
            editor->MarkDirty();
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
    std::function<void(bool)> onComplete) {
    try {
        if (absoluteDestination.empty() || !absoluteDestination.is_absolute()) {
            QueueUiNotification(
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
            std::move(onComplete));
    } catch (...) {
        m_SaveTaskState = Async::TaskState::Failed;
        m_SaveStatusText = "Failed to prepare the project save.";
        QueueUiNotification(
            UiNotificationSeverity::Error,
            m_SaveStatusText,
            "library-save-project-path");
        if (editor) {
            editor->MarkDirty();
        }
        if (onComplete) onComplete(false);
    }
}

void LibraryManager::RequestSaveProjectImpl(
    const std::string& name,
    EditorModule* editor,
    const std::string& existingFileName,
    const std::filesystem::path& absoluteDestination,
    std::function<void(bool)> onComplete) {
    if (!editor || Async::IsBusy(m_SaveTaskState)) {
        QueueUiNotification(UiNotificationSeverity::Error, "Failed to save the project to the library.", "library-save-project");
        if (onComplete) onComplete(false);
        return;
    }

    m_SaveTaskState = Async::TaskState::Applying;
    m_SaveStatusText = "Capturing the project snapshot for the library...";

    const std::string trimmedName = TrimWhitespace(name).empty() ? "Untitled Project" : TrimWhitespace(name);
    const StackFormat::json pipeline = editor->SerializePipeline();
    const std::vector<StackFormat::NodeBrowserThumbnailEntry> nodeBrowserThumbnailEntries =
        editor->GetPersistedNodeBrowserThumbnails();

    int renderedW = 0;
    int renderedH = 0;
    std::vector<unsigned char> renderedPixels;
    int sourceW = 0;
    int sourceH = 0;
    std::vector<unsigned char> sourcePixels;
    std::vector<unsigned char> sourcePngBytesOverride;

    const bool compositeProject = editor->IsCompositeViewportMode();
    if (compositeProject) {
        editor->BuildCompositeExportRaster(renderedPixels, renderedW, renderedH);
        if (!renderedPixels.empty() && renderedW > 0 && renderedH > 0) {
            sourceW = renderedW;
            sourceH = renderedH;
            sourcePixels =
                Stack::PixelBuffer::BuildTransparentRgbaPixels(sourceW, sourceH);
        }
    } else {
        if (editor->IsRenderOnlyUpToActive()) {
            editor->GetPipeline().Execute(editor->GetLayers());
        }
        renderedPixels = editor->GetPipeline().GetOutputPixels(renderedW, renderedH);
        if ((renderedPixels.empty() || renderedW <= 0 || renderedH <= 0) && editor->GetNodeGraph().IsOutputConnected()) {
            editor->BuildSingleOutputExportRaster(renderedPixels, renderedW, renderedH);
        }
        if (!renderedPixels.empty() && renderedW > 0 && renderedH > 0) {
            sourcePixels = editor->GetPipeline().GetSourcePixels(sourceW, sourceH);
            std::vector<unsigned char> graphSourcePngBytes;
            if (LibraryImage::ExtractEmbeddedGraphSourcePng(pipeline, graphSourcePngBytes)) {
                std::vector<unsigned char> decodedGraphSourcePixels;
                int graphSourceW = 0;
                int graphSourceH = 0;
                int graphSourceChannels = 0;
                if (DecodeImageBytes(graphSourcePngBytes, decodedGraphSourcePixels, graphSourceW, graphSourceH, graphSourceChannels) &&
                    !decodedGraphSourcePixels.empty() &&
                    graphSourceW > 0 &&
                    graphSourceH > 0) {
                    sourcePngBytesOverride = std::move(graphSourcePngBytes);
                    sourcePixels = std::move(decodedGraphSourcePixels);
                    sourceW = graphSourceW;
                    sourceH = graphSourceH;
                }
            }
            if (sourcePixels.empty() || sourceW <= 0 || sourceH <= 0) {
                sourceW = renderedW;
                sourceH = renderedH;
                sourcePixels =
                    Stack::PixelBuffer::BuildTransparentRgbaPixels(sourceW, sourceH);
            }
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
        const std::string safeStem = SanitizeFileStem(trimmedName);
        fileName = safeStem + "_" + std::to_string(std::time(nullptr)) + ".stack";
    }
    if (projectPath.empty()) {
        projectPath = (m_LibraryPath / fileName).lexically_normal();
    } else {
        fileName = projectPath.string();
    }
    const bool publishLibraryArtifacts =
        projectPath.parent_path().lexically_normal() ==
        m_LibraryPath.lexically_normal();
    const std::string assetFileName =
        BuildAssetPathForProjectFile(projectPath.filename().string())
            .filename()
            .string();

    ++m_SaveGeneration;
    const std::uint64_t generation = m_SaveGeneration;
    m_SaveTaskState = Async::TaskState::Running;
    m_SaveStatusText = "Packaging and writing project files in the background...";

    bool submitted = false;
    try {
        submitted = Async::TaskSystem::Get().Submit([this,
                                         generation,
                                         trimmedName,
                                         fileName,
                                         projectPath,
                                         publishLibraryArtifacts,
                                         assetFileName,
                                         pipeline = std::move(pipeline),
                                         nodeBrowserThumbnailEntries = std::move(nodeBrowserThumbnailEntries),
                                         renderedW,
                                         renderedH,
                                         renderedPixels = std::move(renderedPixels),
                                         sourceW,
                                         sourceH,
                                         sourcePixels = std::move(sourcePixels),
                                         sourcePngBytesOverride = std::move(sourcePngBytesOverride),
                                         editor,
                                         onComplete]() mutable {
        bool wroteProject = false;
        bool wroteAsset = false;

        try {
            std::vector<unsigned char> sourcePngBytes = std::move(sourcePngBytesOverride);
            if (sourcePngBytes.empty()) {
                sourcePngBytes = Stack::PngEncoding::EncodeInterleaved(
                    sourcePixels, sourceW, sourceH, 4);
            }
            if (sourcePngBytes.empty()) {
                throw std::runtime_error(
                    "Failed to encode the project source image.");
            }

            StackFormat::ProjectDocument document;
            document.metadata.projectKind = StackFormat::kEditorProjectKind;
            document.metadata.projectName = trimmedName;
            document.metadata.timestamp = BuildTimestampString();
            document.metadata.sourceWidth = sourceW;
            document.metadata.sourceHeight = sourceH;
            document.thumbnailBytes = GenerateThumbnailBytes(renderedPixels, renderedW, renderedH);
            document.sourceImageBytes = std::move(sourcePngBytes);
            document.pipelineData = pipeline;
            document.nodeBrowserThumbnailEntries = nodeBrowserThumbnailEntries;

            {
                std::lock_guard<std::mutex> fileLock(m_ProjectFileIoMutex);
                wroteProject = StackFormat::WriteProjectFile(projectPath, document);
            }
            if (wroteProject && publishLibraryArtifacts) {
                const std::vector<unsigned char> renderedPngBytes =
                    Stack::PngEncoding::EncodeInterleaved(
                        renderedPixels, renderedW, renderedH, 4);
                wroteAsset = !renderedPngBytes.empty() &&
                    WriteFileBytes(
                        m_AssetsPath / assetFileName,
                        renderedPngBytes);

                if (wroteAsset) {
                    std::string hash = ComputeImageHash(renderedPngBytes);
                    nlohmann::json meta = {
                        {"hash", hash},
                        {"projectFileName", fileName},
                        {"displayName", trimmedName},
                        {"timestamp", document.metadata.timestamp},
                        {"width", renderedW},
                        {"height", renderedH}
                    };
                    std::ofstream f(m_AssetsPath / (assetFileName + ".hash"));
                    if (f.is_open()) {
                        f << meta.dump(4);
                    }
                }

                // Sync all other embedded assets inside the project
                SyncProjectAssets(fileName, document);
            } else if (wroteProject) {
                // An externally located .stack file is self-contained. Do
                // not create or overwrite managed-Library sidecars merely
                // because that project is currently open in the Editor.
                wroteAsset = true;
            }
        } catch (...) {
            wroteProject = false;
            wroteAsset = false;
        }

        Async::TaskSystem::Get().PostToMain([
            this,
            generation,
            wroteProject,
            wroteAsset,
            trimmedName,
            fileName,
            publishLibraryArtifacts,
            editor,
            onComplete = std::move(onComplete)
        ]() {
            if (generation != m_SaveGeneration) {
                if (onComplete) onComplete(false);
                return;
            }

            if (wroteProject && wroteAsset) {
                m_SaveTaskState = Async::TaskState::Idle;
                m_SaveStatusText = publishLibraryArtifacts
                    ? "Project saved to the library."
                    : "Project saved.";
                QueueUiNotification(
                    UiNotificationSeverity::Success,
                    m_SaveStatusText,
                    "library-save-project");
                if (publishLibraryArtifacts) {
                    m_LastLibrarySignature = 0;
                }
                if (editor) {
                    editor->SetCurrentProjectName(trimmedName);
                    editor->SetCurrentProjectFileName(fileName);
                    editor->ClearDirty();
                }
                if (onComplete) onComplete(true);
            } else {
                m_SaveTaskState = Async::TaskState::Failed;
                m_SaveStatusText = publishLibraryArtifacts
                    ? "Failed to save the project to the library."
                    : "Failed to save the project.";
                QueueUiNotification(
                    UiNotificationSeverity::Error,
                    m_SaveStatusText,
                    "library-save-project");
                if (editor) {
                    editor->MarkDirty();
                }
                if (onComplete) onComplete(false);
            }
        });
        });
    } catch (...) {
        submitted = false;
    }
    if (!submitted && generation == m_SaveGeneration) {
        m_SaveTaskState = Async::TaskState::Failed;
        m_SaveStatusText = "The project save could not be queued.";
        QueueUiNotification(
            UiNotificationSeverity::Error,
            m_SaveStatusText,
            "library-save-project");
        editor->MarkDirty();
        if (onComplete) onComplete(false);
    }
}

void LibraryManager::RequestPersistNodeBrowserThumbnails(
    const std::string& fileName,
    std::vector<StackFormat::NodeBrowserThumbnailEntry> entries) {
    if (fileName.empty()) {
        return;
    }

    const std::filesystem::path projectPath = m_LibraryPath / fileName;
    std::uint64_t revision = 0;
    try {
        revision = BumpNodeBrowserThumbnailPersistRevision(projectPath);
    } catch (...) {
        return;
    }
    bool submitted = false;
    try {
        submitted = Async::TaskSystem::Get().Submit(
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
            success = StackFormat::ReadProjectFile(projectPath, document, options) ||
                LoadLegacyProjectDocument(projectPath, document, options);
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

void LibraryManager::RequestLoadProject(const std::string& fileName, EditorModule* editor, std::function<void(bool)> onComplete) {
    if (fileName.empty() || !editor) {
        QueueUiNotification(UiNotificationSeverity::Error, "Failed to load the selected project.", "library-load-project");
        if (onComplete) onComplete(false);
        return;
    }

    ++m_ProjectLoadGeneration;
    const std::uint64_t generation = m_ProjectLoadGeneration;
    m_ProjectLoadTaskState = Async::TaskState::Queued;
    m_ProjectLoadStatusText = "Loading the project in the background...";

    bool submitted = false;
    try {
        submitted = Async::TaskSystem::Get().Submit(
            [this, generation, fileName, editor, onComplete]() mutable {
        StackFormat::ProjectDocument document;
        EditorModule::LoadedProjectData loadedProject;
        bool success = false;
        try {
            StackFormat::ProjectLoadOptions options;
            options.includeThumbnail = false;
            options.includeSourceImage = true;
            options.includePipelineData = true;
            options.includeNodeBrowserThumbnails = true;

            success = LoadProjectDocument(fileName, document, options);
            if (success &&
                (document.metadata.projectKind == StackFormat::kRenderProjectKind ||
                 document.metadata.projectKind == StackFormat::kCompositeProjectKind)) {
                success = false;
            }
            if (success) {
                const bool isRawProject =
                    document.metadata.projectKind == StackFormat::kRawProjectKind ||
                    (document.rawWorkspaceData.is_object() &&
                     document.rawWorkspaceData.value("schema", std::string()) ==
                         "stack.rawWorkspace.project");
                int width = 0;
                int height = 0;
                int channels = 0;
                if (isRawProject) {
                    loadedProject.sourcePixels.assign(4, 0);
                    width = 1;
                    height = 1;
                    channels = 4;
                } else {
                    success = DecodeImageBytes(
                        document.sourceImageBytes,
                        loadedProject.sourcePixels,
                        width,
                        height,
                        channels);
                }
                if (success) {
                    loadedProject.width = width;
                    loadedProject.height = height;
                    loadedProject.channels = channels;
                    loadedProject.pipelineData = document.pipelineData.is_null()
                        ? StackFormat::json::array()
                        : document.pipelineData;
                    if (isRawProject && loadedProject.pipelineData.empty() &&
                        document.rawWorkspaceData.is_object() &&
                        document.rawWorkspaceData.contains("downstreamGraph")) {
                        loadedProject.pipelineData =
                            document.rawWorkspaceData["downstreamGraph"];
                    }
                    loadedProject.projectName = document.metadata.projectName;
                    loadedProject.projectFileName = isRawProject
                        ? (m_LibraryPath / fileName).lexically_normal().string()
                        : fileName;
                    loadedProject.projectKind = isRawProject
                        ? StackFormat::kRawProjectKind
                        : document.metadata.projectKind;
                    loadedProject.rawWorkspaceData =
                        std::move(document.rawWorkspaceData);
                    loadedProject.projectStore = document.projectStore;
                    loadedProject.rawProjectSnapshot = document.rawProjectSnapshot;
                    loadedProject.nodeBrowserThumbnailEntries =
                        std::move(document.nodeBrowserThumbnailEntries);
                }
            }
        } catch (...) {
            success = false;
        }

        Async::TaskSystem::Get().PostToMain([this,
                                             generation,
                                             editor,
                                             onComplete = std::move(onComplete),
                                             loadedProject = std::move(loadedProject),
                                             success]() mutable {
            if (generation != m_ProjectLoadGeneration) {
                if (onComplete) onComplete(false);
                return;
            }

            try {
                if (!success) {
                    m_ProjectLoadTaskState = Async::TaskState::Failed;
                    m_ProjectLoadStatusText =
                        "Failed to load the selected project.";
                    QueueUiNotification(
                        UiNotificationSeverity::Error,
                        "Failed to load the selected project.",
                        "library-load-project");
                    if (onComplete) onComplete(false);
                    return;
                }

                auto projectData = std::make_shared<EditorLoadedProjectData>(
                    std::move(loadedProject));
                m_ProjectLoadTaskState = Async::TaskState::Applying;
                m_ProjectLoadStatusText =
                    "Applying project data to the editor...";

                const bool started =
                    editor->BeginDeferredLoadedProjectApply(projectData);
                if (started) {
                    m_ProjectLoadTaskState = Async::TaskState::Idle;
                    m_ProjectLoadStatusText = "Project load handed to editor.";
                    QueueUiNotification(
                        UiNotificationSeverity::Success,
                        "Project load started.",
                        "library-load-project");
                } else {
                    m_ProjectLoadTaskState = Async::TaskState::Failed;
                    m_ProjectLoadStatusText =
                        "Failed to apply the loaded project.";
                    QueueUiNotification(
                        UiNotificationSeverity::Error,
                        "Failed to apply the loaded project.",
                        "library-load-project");
                }

                if (onComplete) onComplete(started);
            } catch (...) {
                m_ProjectLoadTaskState = Async::TaskState::Failed;
                try {
                    m_ProjectLoadStatusText =
                        "Failed to apply the loaded project.";
                    QueueUiNotification(
                        UiNotificationSeverity::Error,
                        m_ProjectLoadStatusText,
                        "library-load-project");
                } catch (...) {
                    m_ProjectLoadStatusText.clear();
                }
                if (onComplete) {
                    try {
                        onComplete(false);
                    } catch (...) {
                    }
                }
            }
        });
            });
    } catch (...) {
        submitted = false;
    }
    if (!submitted && generation == m_ProjectLoadGeneration) {
        m_ProjectLoadTaskState = Async::TaskState::Failed;
        m_ProjectLoadStatusText = "The project load could not be queued.";
        QueueUiNotification(
            UiNotificationSeverity::Error,
            m_ProjectLoadStatusText,
            "library-load-project");
        if (onComplete) onComplete(false);
    }
}

void LibraryManager::RequestLoadProjectDeferredApply(
    const std::string& fileName,
    std::function<void(bool, std::shared_ptr<EditorLoadedProjectData>)> onReady) {
    if (fileName.empty()) {
        QueueUiNotification(UiNotificationSeverity::Error, "Failed to load the selected project.", "library-load-project");
        if (onReady) onReady(false, nullptr);
        return;
    }

    ++m_ProjectLoadGeneration;
    const std::uint64_t generation = m_ProjectLoadGeneration;
    m_ProjectLoadTaskState = Async::TaskState::Queued;
    m_ProjectLoadStatusText = "Loading the project in the background...";

    bool submitted = false;
    try {
        submitted = Async::TaskSystem::Get().Submit(
            [this, generation, fileName, onReady]() mutable {
        StackFormat::ProjectDocument document;
        std::shared_ptr<EditorLoadedProjectData> loadedProject;
        bool success = false;
        try {
            StackFormat::ProjectLoadOptions options;
            options.includeThumbnail = false;
            options.includeSourceImage = true;
            options.includePipelineData = true;
            options.includeNodeBrowserThumbnails = true;

            loadedProject = std::make_shared<EditorLoadedProjectData>();
            success = LoadProjectDocument(fileName, document, options);
            if (success &&
                (document.metadata.projectKind == StackFormat::kRenderProjectKind ||
                 document.metadata.projectKind == StackFormat::kCompositeProjectKind)) {
                success = false;
            }
            if (success) {
                const bool isRawProject =
                    document.metadata.projectKind == StackFormat::kRawProjectKind ||
                    (document.rawWorkspaceData.is_object() &&
                     document.rawWorkspaceData.value("schema", std::string()) ==
                         "stack.rawWorkspace.project");
                int width = 0;
                int height = 0;
                int channels = 0;
                if (isRawProject) {
                    loadedProject->sourcePixels.assign(4, 0);
                    width = 1;
                    height = 1;
                    channels = 4;
                } else {
                    success = DecodeImageBytes(
                        document.sourceImageBytes,
                        loadedProject->sourcePixels,
                        width,
                        height,
                        channels);
                }
                if (success) {
                    loadedProject->width = width;
                    loadedProject->height = height;
                    loadedProject->channels = channels;
                    loadedProject->pipelineData = document.pipelineData.is_null()
                        ? StackFormat::json::array()
                        : document.pipelineData;
                    if (isRawProject && loadedProject->pipelineData.empty() &&
                        document.rawWorkspaceData.is_object() &&
                        document.rawWorkspaceData.contains("downstreamGraph")) {
                        loadedProject->pipelineData =
                            document.rawWorkspaceData["downstreamGraph"];
                    }
                    loadedProject->projectName = document.metadata.projectName;
                    loadedProject->projectFileName = isRawProject
                        ? (m_LibraryPath / fileName).lexically_normal().string()
                        : fileName;
                    loadedProject->projectKind = isRawProject
                        ? StackFormat::kRawProjectKind
                        : document.metadata.projectKind;
                    loadedProject->rawWorkspaceData =
                        std::move(document.rawWorkspaceData);
                    loadedProject->projectStore = document.projectStore;
                    loadedProject->rawProjectSnapshot = document.rawProjectSnapshot;
                    loadedProject->nodeBrowserThumbnailEntries =
                        std::move(document.nodeBrowserThumbnailEntries);
                }
            }
        } catch (...) {
            loadedProject.reset();
            success = false;
        }

        Async::TaskSystem::Get().PostToMain([this,
                                             generation,
                                             onReady = std::move(onReady),
                                             loadedProject,
                                             success]() mutable {
            if (generation != m_ProjectLoadGeneration) {
                if (onReady) onReady(false, nullptr);
                return;
            }

            if (!success) {
                m_ProjectLoadTaskState = Async::TaskState::Failed;
                m_ProjectLoadStatusText = "Failed to load the selected project.";
                QueueUiNotification(UiNotificationSeverity::Error, "Failed to load the selected project.", "library-load-project");
                if (onReady) onReady(false, nullptr);
                return;
            }

            m_ProjectLoadTaskState = Async::TaskState::Applying;
            m_ProjectLoadStatusText = "Project data decoded.";

            if (onReady) {
                onReady(true, std::move(loadedProject));
            }
        });
            });
    } catch (...) {
        submitted = false;
    }
    if (!submitted && generation == m_ProjectLoadGeneration) {
        m_ProjectLoadTaskState = Async::TaskState::Failed;
        m_ProjectLoadStatusText = "The project load could not be queued.";
        QueueUiNotification(
            UiNotificationSeverity::Error,
            m_ProjectLoadStatusText,
            "library-load-project");
        if (onReady) onReady(false, nullptr);
    }
}

void LibraryManager::SetProjectLoadApplyingStatus(const std::string& statusText) {
    m_ProjectLoadTaskState = Async::TaskState::Applying;
    m_ProjectLoadStatusText = statusText;
}

void LibraryManager::FinishDeferredProjectLoad(bool success, const std::string& message) {
    if (success) {
        m_ProjectLoadTaskState = Async::TaskState::Idle;
        m_ProjectLoadStatusText = message.empty() ? "Project loaded into the editor." : message;
        QueueUiNotification(UiNotificationSeverity::Success, m_ProjectLoadStatusText, "library-load-project");
        return;
    }

    m_ProjectLoadTaskState = Async::TaskState::Failed;
    m_ProjectLoadStatusText = message.empty() ? "Failed to apply the loaded project." : message;
    QueueUiNotification(UiNotificationSeverity::Error, m_ProjectLoadStatusText, "library-load-project");
}

bool LibraryManager::RenameProject(const std::string& fileName, const std::string& newName) {
    const std::string trimmedName = TrimWhitespace(newName);
    if (trimmedName.empty()) return false;

    StackFormat::ProjectDocument document;
    if (!LoadProjectDocument(fileName, document, {})) return false;

    document.metadata.projectName = trimmedName;
    document.metadata.timestamp = BuildTimestampString();

    if (!StackFormat::WriteProjectFile(m_LibraryPath / fileName, document)) {
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
        const std::filesystem::path projectPath = m_LibraryPath / fileName;
        if (!std::filesystem::exists(projectPath)) return false;

        StackFormat::ProjectDocument document;
        StackFormat::ProjectLoadOptions metadataOnly { true, false, false };
        const bool loadedMetadata = LoadProjectDocument(fileName, document, metadataOnly);
        const bool removedProject = std::filesystem::remove(projectPath);
        TagManager::Get().SetTags(fileName, {});
        if (!loadedMetadata || document.metadata.projectKind != StackFormat::kCompositeProjectKind) {
            const std::filesystem::path assetPath = BuildAssetPathForProjectFile(fileName);
            if (std::filesystem::exists(assetPath)) {
                std::error_code ec;
                std::filesystem::remove(assetPath, ec);
                std::filesystem::remove(assetPath.string() + ".hash", ec);
                TagManager::Get().SetTags(assetPath.filename().string(), {});
            }
        }

        m_LastLibrarySignature = 0;
        return removedProject;
    } catch (...) {
        return false;
    }
}

std::filesystem::path LibraryManager::BuildAssetPathForProjectFile(const std::string& projectFileName) const {
    return m_AssetsPath / (std::filesystem::path(projectFileName).stem().string() + ".png");
}

void LibraryManager::RequestLoadProjectFromPath(const std::filesystem::path& absolutePath, EditorModule* editor, std::function<void(bool)> onComplete) {
    if (absolutePath.empty() || !editor) {
        QueueUiNotification(UiNotificationSeverity::Error, "Failed to load the project from disk.", "library-load-project-path");
        if (onComplete) onComplete(false);
        return;
    }

    ++m_ProjectLoadGeneration;
    const std::uint64_t generation = m_ProjectLoadGeneration;
    m_ProjectLoadTaskState = Async::TaskState::Queued;
    m_ProjectLoadStatusText = "Loading the project from path...";

    bool submitted = false;
    try {
        submitted = Async::TaskSystem::Get().Submit(
            [this, generation, absolutePath, editor, onComplete]() mutable {
        StackFormat::ProjectDocument document;
        EditorModule::LoadedProjectData loadedProject;

        bool success = false;
        try {
            StackFormat::ProjectLoadOptions options;
            options.includeThumbnail = false;
            options.includeSourceImage = true;
            options.includePipelineData = true;
            options.includeNodeBrowserThumbnails = true;

            if (std::filesystem::exists(absolutePath)) {
                std::lock_guard<std::mutex> fileLock(m_ProjectFileIoMutex);
                if (StackFormat::ReadProjectFile(
                        absolutePath,
                        document,
                        options)) {
                    success = true;
                } else {
                    success = LoadLegacyProjectDocument(
                        absolutePath,
                        document,
                        options);
                }
            }

            if (success &&
                (document.metadata.projectKind == StackFormat::kRenderProjectKind ||
                 document.metadata.projectKind == StackFormat::kCompositeProjectKind)) {
                success = false;
            }
            if (success) {
                const bool isRawProject =
                    document.metadata.projectKind == StackFormat::kRawProjectKind ||
                    (document.rawWorkspaceData.is_object() &&
                     document.rawWorkspaceData.value("schema", std::string()) ==
                         "stack.rawWorkspace.project");
                int width = 0;
                int height = 0;
                int channels = 0;
                if (isRawProject) {
                    loadedProject.sourcePixels.assign(4, 0);
                    width = 1;
                    height = 1;
                    channels = 4;
                } else {
                    success = DecodeImageBytes(
                        document.sourceImageBytes,
                        loadedProject.sourcePixels,
                        width,
                        height,
                        channels);
                }
                if (success) {
                    loadedProject.width = width;
                    loadedProject.height = height;
                    loadedProject.channels = channels;
                    loadedProject.pipelineData = document.pipelineData.is_null()
                        ? StackFormat::json::array()
                        : document.pipelineData;
                    if (isRawProject && loadedProject.pipelineData.empty() &&
                        document.rawWorkspaceData.is_object() &&
                        document.rawWorkspaceData.contains("downstreamGraph")) {
                        loadedProject.pipelineData =
                            document.rawWorkspaceData["downstreamGraph"];
                    }
                    loadedProject.projectName = document.metadata.projectName;
                    loadedProject.projectFileName =
                        absolutePath.lexically_normal().string();
                    loadedProject.projectKind = isRawProject
                        ? StackFormat::kRawProjectKind
                        : document.metadata.projectKind;
                    loadedProject.rawWorkspaceData =
                        std::move(document.rawWorkspaceData);
                    loadedProject.projectStore = document.projectStore;
                    loadedProject.rawProjectSnapshot = document.rawProjectSnapshot;
                    loadedProject.nodeBrowserThumbnailEntries =
                        std::move(document.nodeBrowserThumbnailEntries);
                }
            }
        } catch (...) {
            success = false;
        }

        Async::TaskSystem::Get().PostToMain([this,
                                             generation,
                                             editor,
                                             onComplete = std::move(onComplete),
                                             loadedProject = std::move(loadedProject),
                                             success]() mutable {
            if (generation != m_ProjectLoadGeneration) {
                if (onComplete) onComplete(false);
                return;
            }

            try {
                if (!success) {
                    m_ProjectLoadTaskState = Async::TaskState::Failed;
                    m_ProjectLoadStatusText =
                        "Failed to load the project from disk.";
                    QueueUiNotification(
                        UiNotificationSeverity::Error,
                        "Failed to load the project from disk.",
                        "library-load-project-path");
                    if (onComplete) onComplete(false);
                    return;
                }

                auto projectData = std::make_shared<EditorLoadedProjectData>(
                    std::move(loadedProject));
                m_ProjectLoadTaskState = Async::TaskState::Applying;
                m_ProjectLoadStatusText = "Applying project data...";

                const bool started =
                    editor->BeginDeferredLoadedProjectApply(projectData);
                if (started) {
                    m_ProjectLoadTaskState = Async::TaskState::Idle;
                    m_ProjectLoadStatusText = "Project load handed to editor.";
                    QueueUiNotification(
                        UiNotificationSeverity::Success,
                        "Project load started.",
                        "library-load-project-path");
                } else {
                    m_ProjectLoadTaskState = Async::TaskState::Failed;
                    m_ProjectLoadStatusText =
                        "Failed to apply project data.";
                    QueueUiNotification(
                        UiNotificationSeverity::Error,
                        "Failed to apply project data.",
                        "library-load-project-path");
                }

                if (onComplete) onComplete(started);
            } catch (...) {
                m_ProjectLoadTaskState = Async::TaskState::Failed;
                try {
                    m_ProjectLoadStatusText =
                        "Failed to apply project data.";
                    QueueUiNotification(
                        UiNotificationSeverity::Error,
                        m_ProjectLoadStatusText,
                        "library-load-project-path");
                } catch (...) {
                    m_ProjectLoadStatusText.clear();
                }
                if (onComplete) {
                    try {
                        onComplete(false);
                    } catch (...) {
                    }
                }
            }
        });
            });
    } catch (...) {
        submitted = false;
    }
    if (!submitted && generation == m_ProjectLoadGeneration) {
        m_ProjectLoadTaskState = Async::TaskState::Failed;
        m_ProjectLoadStatusText = "The project load could not be queued.";
        QueueUiNotification(
            UiNotificationSeverity::Error,
            m_ProjectLoadStatusText,
            "library-load-project-path");
        if (onComplete) onComplete(false);
    }
}
