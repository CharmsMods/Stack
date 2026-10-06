#include "Editor/EditorModule.h"

#include <limits>
#include <utility>

bool EditorModule::AdoptSavedProjectStore(
    Stack::Project::ProjectStoreHandle store,
    Stack::Project::RawProjectSnapshot snapshot,
    const std::string& expectedDocumentId,
    std::uint64_t capturedEditRevision) {
    if (!store || expectedDocumentId.empty() ||
        m_Project->documentId != expectedDocumentId || snapshot.projectId != expectedDocumentId ||
        m_Project->editRevision < capturedEditRevision ||
        snapshot.projectKindHint != StackBinaryFormat::kEditorProjectKind) return false;
    const auto editsSinceCapture = m_Project->editRevision - capturedEditRevision;
    const auto persistedDirtyRevision = snapshot.dirtyRevision;
    if (editsSinceCapture > std::numeric_limits<std::uint64_t>::max() - persistedDirtyRevision)
        return false;
    const auto currentDirtyRevision = persistedDirtyRevision + editsSinceCapture;
    auto saved = std::make_shared<Stack::Project::RawProjectSnapshot>(std::move(snapshot));
    if (!m_Project->lifecycle.AdoptSavedBaseline(expectedDocumentId,
            persistedDirtyRevision, currentDirtyRevision, saved->persistedStorageRevision)) return false;
    // The stored snapshot remains the base for the next managed save. Newer
    // authored edits stay in the project graph and in the lifecycle revision.
    saved->dirtyRevision = currentDirtyRevision;
    m_Project->store = std::move(store);
    m_Project->snapshot = std::move(saved);
    m_Project->storePath = m_Project->store->StoragePath();
    m_Project->rawPipelineActive = false;
    m_Project->dirty = editsSinceCapture != 0;
    return true;
}
