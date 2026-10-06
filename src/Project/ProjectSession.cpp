#include "Project/ProjectSession.h"

#include "Editor/Layers/LayerBase.h"

namespace Stack::Project {

ProjectSession::ProjectSession() = default;
ProjectSession::~ProjectSession() = default;

const std::string& ProjectSession::EnsureDocumentId() {
    if (documentId.empty()) {
        if (snapshot && !snapshot->projectId.empty()) {
            documentId = snapshot->projectId;
        } else if (!lifecycle.ProjectId().empty()) {
            documentId = lifecycle.ProjectId();
        } else {
            documentId = GenerateStableUuid();
        }
    }
    return documentId;
}

void ProjectSession::NoteEdit(double nowSeconds) {
    dirty = true;
    // A save captures this revision. A completion may clear only that edit,
    // even if more edits arrive while the writer is running.
    ++editRevision;
    lastEditTime = nowSeconds;
    EnsureDocumentId();
    if (snapshot) snapshot->dirtyRevision = lifecycle.NoteEdit();
}

bool ProjectSession::ClearDirtyIfRevision(std::uint64_t expectedRevision) {
    if (editRevision != expectedRevision) return false;
    dirty = false;
    return true;
}

} // namespace Stack::Project
