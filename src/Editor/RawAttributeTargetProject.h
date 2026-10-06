#pragma once

#include "Persistence/RawProjectAttributeTransfer.h"
#include "Raw/RawWorkspace.h"

#include <filesystem>

namespace Stack::EditorRawAttributes {

struct CreatedTargetProjectResult {
    Stack::Project::RawProjectAttributeTransferResult transfer;
    std::filesystem::path projectPath;
};

CreatedTargetProjectResult CreateProjectAndPasteAttributes(
    const std::filesystem::path& workspaceRoot,
    const Stack::RawWorkspace::SourceRecord& source,
    const Stack::RawRecipe::RawEditAttributeBundle& bundle,
    const std::vector<std::string>& selectedKeys,
    const std::filesystem::path& projectsDirectoryOverride = {});

// Existing targets are never overwritten by the paste workflow. The complete
// project (graph, embedded originals, recipes, and unknown fields) is copied
// first, then the requested attributes are committed to that independent copy.
CreatedTargetProjectResult DuplicateProjectAndPasteAttributes(
    const std::filesystem::path& sourceProjectPath,
    const std::string& sourceSetId,
    const Stack::RawRecipe::RawEditAttributeBundle& bundle,
    const std::vector<std::string>& selectedKeys,
    const Stack::Project::ProjectStoreHandle& sourceStoreOverride = {},
    const Stack::Project::RawProjectSnapshot* sourceSnapshotOverride = nullptr);

} // namespace Stack::EditorRawAttributes
