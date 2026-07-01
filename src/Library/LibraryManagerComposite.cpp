#include "LibraryManager.h"

#include "Async/TaskSystem.h"
#include "Composite/CompositeModule.h"
#include "Library/Internal/LibraryStorageHelpers.h"

#include <ctime>
#include <filesystem>
#include <system_error>
#include <utility>

namespace {

namespace StackFormat = StackBinaryFormat;

} // namespace

using namespace Stack::Library::StorageHelpers;

void LibraryManager::RequestSaveCompositeProject(
    const std::string& name,
    CompositeModule* composite,
    const std::string& existingFileName,
    std::function<void(bool)> onComplete) {
    if (!composite || Async::IsBusy(m_SaveTaskState) || !composite->HasLayers()) {
        if (!composite || !composite->HasLayers()) {
            m_SaveTaskState = Async::TaskState::Failed;
            m_SaveStatusText = "Add at least one layer before saving a composite project.";
            QueueUiNotification(UiNotificationSeverity::Error, "Add at least one layer before saving a composite project.", "library-save-composite");
        }
        if (onComplete) onComplete(false);
        return;
    }

    m_SaveTaskState = Async::TaskState::Applying;
    m_SaveStatusText = "Building composite project for the library...";

    const std::string trimmedName = TrimWhitespace(name).empty() ? "Untitled Composite" : TrimWhitespace(name);
    StackFormat::ProjectDocument document;
    if (!composite->BuildProjectDocumentForSave(trimmedName, document)) {
        m_SaveTaskState = Async::TaskState::Failed;
        m_SaveStatusText = "Could not rasterize the composite for saving.";
        QueueUiNotification(UiNotificationSeverity::Error, "Could not rasterize the composite for saving.", "library-save-composite");
        return;
    }

    document.metadata.timestamp = BuildTimestampString();

    std::string fileName = existingFileName;
    if (fileName.empty() && composite && !composite->GetCurrentProjectFileName().empty()) {
        fileName = composite->GetCurrentProjectFileName();
    }
    const std::string safeStem = SanitizeFileStem(trimmedName);
    const std::string fallbackStem = safeStem + "_" + std::to_string(std::time(nullptr));
    fileName = EnsureProjectFileNameForKind(
        fileName,
        fallbackStem,
        StackFormat::kCompositeProjectKind);
    const std::string legacyProjectFileToDelete =
        (!existingFileName.empty() &&
         existingFileName != fileName &&
         std::filesystem::path(existingFileName).extension() == ".stack")
            ? existingFileName
            : std::string();

    ++m_SaveGeneration;
    const std::uint64_t generation = m_SaveGeneration;
    m_SaveTaskState = Async::TaskState::Running;
    m_SaveStatusText = "Writing composite project files in the background...";

    Async::TaskSystem::Get().Submit([this,
                                     generation,
                                     fileName,
                                     legacyProjectFileToDelete,
                                     trimmedName,
                                     document = std::move(document),
                                     composite,
                                     onComplete = std::move(onComplete)]() mutable {
        bool wroteProject = false;

        try {
            wroteProject = StackFormat::WriteProjectFile(m_LibraryPath / fileName, document);
            if (wroteProject && !legacyProjectFileToDelete.empty()) {
                std::error_code ec;
                std::filesystem::remove(m_LibraryPath / legacyProjectFileToDelete, ec);
            }
        } catch (...) {
            wroteProject = false;
        }

        Async::TaskSystem::Get().PostToMain([this, generation, wroteProject, fileName, trimmedName, composite, onComplete = std::move(onComplete)]() {
            if (generation != m_SaveGeneration) {
                return;
            }

            if (wroteProject) {
                m_SaveTaskState = Async::TaskState::Idle;
                m_SaveStatusText = "Composite project saved to the library.";
                QueueUiNotification(UiNotificationSeverity::Success, "Composite project saved to the library.", "library-save-composite");
                m_LastLibrarySignature = 0;
                RequestRefreshLibraryAsync();
                QueueSavedProjectEvent(fileName, StackFormat::kCompositeProjectKind);
                if (composite) {
                    composite->SetCurrentProjectName(trimmedName);
                    composite->SetCurrentProjectFileName(fileName);
                    composite->ClearDirty();
                }
                if (onComplete) onComplete(true);
            } else {
                m_SaveTaskState = Async::TaskState::Failed;
                m_SaveStatusText = "Failed to save the composite project to the library.";
                QueueUiNotification(UiNotificationSeverity::Error, "Failed to save the composite project to the library.", "library-save-composite");
                if (onComplete) onComplete(false);
            }
        });
    });
}

void LibraryManager::RequestLoadCompositeProject(
    const std::string& fileName,
    CompositeModule* composite,
    std::function<void(bool)> onComplete) {
    if (fileName.empty() || !composite) {
        QueueUiNotification(UiNotificationSeverity::Error, "Failed to load the composite project.", "library-load-composite");
        if (onComplete) onComplete(false);
        return;
    }

    ++m_ProjectLoadGeneration;
    const std::uint64_t generation = m_ProjectLoadGeneration;
    m_ProjectLoadTaskState = Async::TaskState::Queued;
    m_ProjectLoadStatusText = "Loading composite project...";

    Async::TaskSystem::Get().Submit([this, generation, fileName, composite, onComplete = std::move(onComplete)]() mutable {
        StackFormat::ProjectLoadOptions options;
        options.includeThumbnail = false;
        options.includeSourceImage = true;
        options.includePipelineData = true;
        options.includeNodeBrowserThumbnails = true;

        StackFormat::ProjectDocument document;
        bool success = LoadProjectDocument(fileName, document, options);
        if (success) {
            success = document.metadata.projectKind == StackFormat::kCompositeProjectKind;
        }

        Async::TaskSystem::Get().PostToMain([this,
                                             generation,
                                             fileName,
                                             composite,
                                             onComplete = std::move(onComplete),
                                             document = std::move(document),
                                             success]() mutable {
            if (generation != m_ProjectLoadGeneration) {
                return;
            }

            if (!success) {
                m_ProjectLoadTaskState = Async::TaskState::Failed;
                m_ProjectLoadStatusText = "Failed to load the composite project.";
                QueueUiNotification(UiNotificationSeverity::Error, "Failed to load the composite project.", "library-load-composite");
                if (onComplete) onComplete(false);
                return;
            }

            m_ProjectLoadTaskState = Async::TaskState::Applying;
            m_ProjectLoadStatusText = "Applying composite project...";

            const bool applied = composite->ApplyLibraryProject(document);
            if (applied) {
                composite->SetCurrentProjectFileName(fileName);
                composite->SetCurrentProjectName(document.metadata.projectName);
                m_ProjectLoadTaskState = Async::TaskState::Idle;
                m_ProjectLoadStatusText = "Composite project loaded.";
                QueueUiNotification(UiNotificationSeverity::Success, "Composite project loaded.", "library-load-composite");
            } else {
                m_ProjectLoadTaskState = Async::TaskState::Failed;
                m_ProjectLoadStatusText = "Failed to apply composite project data.";
                QueueUiNotification(UiNotificationSeverity::Error, "Failed to apply composite project data.", "library-load-composite");
            }

            if (onComplete) onComplete(applied);
        });
    });
}

void LibraryManager::RequestLoadCompositeProjectFromPath(
    const std::filesystem::path& absolutePath,
    CompositeModule* composite,
    std::function<void(bool)> onComplete) {
    if (absolutePath.empty() || !composite) {
        QueueUiNotification(UiNotificationSeverity::Error, "Failed to load composite project from disk.", "library-load-composite-path");
        if (onComplete) onComplete(false);
        return;
    }

    ++m_ProjectLoadGeneration;
    const std::uint64_t generation = m_ProjectLoadGeneration;
    m_ProjectLoadTaskState = Async::TaskState::Queued;
    m_ProjectLoadStatusText = "Loading composite project from disk...";

    Async::TaskSystem::Get().Submit([this, generation, absolutePath, composite, onComplete = std::move(onComplete)]() mutable {
        StackFormat::ProjectLoadOptions options;
        options.includeThumbnail = false;
        options.includeSourceImage = true;
        options.includePipelineData = true;

        StackFormat::ProjectDocument document;
        bool success = false;
        if (std::filesystem::exists(absolutePath)) {
            if (StackFormat::ReadProjectFile(absolutePath, document, options)) {
                success = true;
            }
        }

        if (success) {
            success = document.metadata.projectKind == StackFormat::kCompositeProjectKind;
        }

        Async::TaskSystem::Get().PostToMain([this,
                                             generation,
                                             composite,
                                             onComplete = std::move(onComplete),
                                             document = std::move(document),
                                             fileName = absolutePath.filename().string(),
                                             success]() mutable {
            if (generation != m_ProjectLoadGeneration) {
                return;
            }

            if (!success) {
                m_ProjectLoadTaskState = Async::TaskState::Failed;
                m_ProjectLoadStatusText = "Failed to load composite project from disk.";
                QueueUiNotification(UiNotificationSeverity::Error, "Failed to load composite project from disk.", "library-load-composite-path");
                if (onComplete) onComplete(false);
                return;
            }

            m_ProjectLoadTaskState = Async::TaskState::Applying;
            m_ProjectLoadStatusText = "Applying composite project...";

            const bool applied = composite->ApplyLibraryProject(document);
            if (applied) {
                composite->SetCurrentProjectFileName(fileName);
                composite->SetCurrentProjectName(document.metadata.projectName);
                m_ProjectLoadTaskState = Async::TaskState::Idle;
                m_ProjectLoadStatusText = "Composite project loaded.";
                QueueUiNotification(UiNotificationSeverity::Success, "Composite project loaded.", "library-load-composite-path");
            } else {
                m_ProjectLoadTaskState = Async::TaskState::Failed;
                m_ProjectLoadStatusText = "Failed to apply composite project data.";
                QueueUiNotification(UiNotificationSeverity::Error, "Failed to apply composite project data.", "library-load-composite-path");
            }

            if (onComplete) onComplete(applied);
        });
    });
}
