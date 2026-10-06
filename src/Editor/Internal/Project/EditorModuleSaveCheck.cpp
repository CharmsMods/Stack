#include "Editor/EditorModule.h"
#include "Async/TaskSystem.h"
#include "Library/LibraryManager.h"

void EditorModule::SetCurrentProjectFileName(const std::string& fileName) {
    m_Project->fileName = fileName;
    m_Project->savedFileStamp.reset();
    if (fileName.empty()) return;
    std::filesystem::path path(fileName);
    if (!path.is_absolute()) path = LibraryManager::Get().GetLibraryPath() / path;
    auto root = Stack::Project::ResolveProjectStoreRoot(path);
    std::error_code error;
    if (!std::filesystem::exists(path, error) && path.extension() == ".stack") {
        // The legacy save entry point may return a .stack name even though
        // its writer created the corresponding directory bundle.
        const auto candidate = path.parent_path() / path.stem();
        if (Stack::Project::IsDirectoryProjectBundle(candidate)) root = candidate;
    }
    if (Stack::Project::IsDirectoryProjectBundle(root))
        path = Stack::Project::WorkingProjectDocumentPath(root);
    m_Project->savedFileStamp = Stack::Project::ProjectFileStamp::Read(path);
}

void EditorModule::CheckCurrentProjectSaveAsync(
    Stack::Project::ProjectSaveCoordinator::Completion completion,
    std::function<void(Stack::Project::ProjectSaveCoordinator::Completion, bool)> write) {
    using namespace Stack::Project;
    const auto store = m_Project->store;
    const std::string projectId = m_Project->documentId;
    const std::uint64_t editRevision = m_Project->editRevision;
    const auto savedStamp = m_Project->savedFileStamp;
    const std::uint64_t expectedRevision = store ? store->StorageRevision() : 0;
    const std::uint64_t generation = ++m_Project->saveCheckGeneration;
    m_Project->saveCheckBusy = true;

    // Use the shared save coordinator to serialize checks and writes. The
    // worker reads only the manifest. An unchanged clean Ctrl+S does not
    // serialize the graph or decode, hash, or write embedded images.
    auto finish = [this, store, projectId, editRevision, expectedRevision,
                   generation, savedStamp, write = std::move(write),
                   completion = std::move(completion)](
                      bool readable, bool missing, std::uint64_t revision,
                      std::string error) mutable {
        if (generation == m_Project->saveCheckGeneration) m_Project->saveCheckBusy = false;
        ProjectSaveResult result;
        result.projectId = projectId;
        result.path = store ? store->StoragePath().string()
            : (savedStamp ? savedStamp->path.string() : std::string());
        result.storageRevision = revision;
        if (projectId != m_Project->documentId || store != m_Project->store) {
            result.status = ProjectSaveStatus::Canceled;
            result.message = "The active project changed during the save check.";
        } else if (missing) {
            // Ordinary sessions retain their source data in memory. Recreate
            // only an absent target, with exclusive creation so a replacement
            // appearing after this check cannot be overwritten. A managed
            // store may have lost its only copy of embedded originals.
            write(std::move(completion), true);
            return;
        } else if (!readable || revision != expectedRevision) {
            result.status = readable ? ProjectSaveStatus::Conflict : ProjectSaveStatus::Failed;
            result.message = readable
                ? "The project changed on disk. Use Save As to preserve your open edits."
                : "Could not check the saved project: " + error;
            PostNotification(UiNotificationSeverity::Error, result.message, "project-save-check");
        } else if (IsDirty() || m_Project->editRevision != editRevision) {
            // Edits made while the disk check ran belong to this Ctrl+S too.
            write(std::move(completion), false);
            return;
        } else {
            result.status = ProjectSaveStatus::Saved;
            result.persistedEditRevision = editRevision;
        }
        completion(std::move(result));
    };
    const bool submitted = ProjectTasks().SubmitHighPriority(
        "Checking Save", [this, store, savedStamp, finish]() mutable {
            std::uint64_t revision = 0;
            std::string error;
            bool readable = false;
            bool missing = false;
            if (store) {
                readable = store->ReadStorageRevision(revision, error);
                if (readable && savedStamp) {
                    const auto current = ProjectFileStamp::Read(savedStamp->path);
                    if (!current || !(*current == *savedStamp)) {
                        readable = false;
                        error = "The project manifest changed on disk. Use Save As to preserve your open edits.";
                    }
                }
                if (!readable) {
                    std::error_code pathError;
                    const auto status = std::filesystem::symlink_status(store->StoragePath(), pathError);
                    missing = status.type() == std::filesystem::file_type::not_found &&
                        (!pathError || pathError == std::errc::no_such_file_or_directory);
                }
            } else if (savedStamp) {
                const auto current = ProjectFileStamp::Read(savedStamp->path);
                readable = current.has_value();
                revision = readable && *current == *savedStamp ? 0 : 1;
                if (!readable) {
                    // A missing manifest inside an existing bundle is damage,
                    // not an absent save target. Do not replace that bundle.
                    std::error_code pathError;
                    const auto status = std::filesystem::symlink_status(
                        ResolveProjectStoreRoot(savedStamp->path), pathError);
                    missing = status.type() == std::filesystem::file_type::not_found &&
                        (!pathError || pathError == std::errc::no_such_file_or_directory);
                    error = "The saved file is missing or cannot be read.";
                }
            }
            ProjectTasks().PostToMain(
                [finish = std::move(finish), readable, missing, revision,
                 error = std::move(error)]() mutable {
                    finish(readable, missing, revision, std::move(error));
                });
        });
    if (!submitted) finish(false, false, 0, "The save check could not be queued.");
}
