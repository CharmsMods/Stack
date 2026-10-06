#pragma once

#include "Persistence/ProjectStore.h"
#include <map>

namespace Stack::Project {

// Creates and commits a project without replacing an editor document.
struct MultiFrameProjectCreation {
    std::filesystem::path path;
    ProjectStorageKind storageKind = ProjectStorageKind::DirectoryBundle;
    std::string projectId;
    std::string projectName;
    std::string sourceSetName = "Captures";
    MultiFrameOperationIntent operationIntent = MultiFrameOperationIntent::RawCaptureSet;
    std::vector<std::filesystem::path> sources;
    std::size_t referenceFrameIndex = 0;
    std::map<int, int> orientationOverrides;
    std::function<bool()> shouldCancel;
    bool resumeEmptyProject = false;
};

enum class ProjectCreationFailure { Input, Storage };
ProjectStoreOpenResult CreateMultiFrameProject(const MultiFrameProjectCreation& request,
    ProjectCreationFailure* failure = nullptr);

} // namespace Stack::Project
