#pragma once

#include "Async/TaskState.h"

#include <cstdint>
#include <string>
#include <utility>

namespace Stack::Project {

// Main-thread state owned by one project instance. Background work carries a
// weak owner ticket and its generation, then publishes on that owner's task group.
struct FileOperation {
    Async::TaskState state = Async::TaskState::Idle;
    std::string statusText;
    std::string warning;
    std::string documentId;
    std::uint64_t generation = 0;
    std::uint64_t editRevision = 0;

    std::uint64_t Begin(std::string ownerDocumentId, std::uint64_t revision) {
        documentId = std::move(ownerDocumentId);
        editRevision = revision;
        state = Async::TaskState::Queued;
        statusText.clear();
        warning.clear();
        return ++generation;
    }

    void Invalidate() {
        ++generation;
        state = Async::TaskState::Idle;
        statusText.clear();
        warning.clear();
        documentId.clear();
        editRevision = 0;
    }
};

struct FileOperationState {
    FileOperation load;
    FileOperation save;
};

} // namespace Stack::Project
