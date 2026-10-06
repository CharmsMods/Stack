#pragma once

#include "Raw/RawEditAttributes.h"

#include <filesystem>
#include <string>
#include <vector>

namespace Stack::Project {

struct RawProjectAttributeTransferResult {
    bool success = false;
    bool changed = false;
    bool skipped = false;
    std::uint64_t committedStorageRevision = 0;
    std::vector<std::string> appliedKeys;
    std::vector<std::string> warnings;
    std::string errorMessage;
};

bool CaptureRawEditAttributesFromProject(
    const std::filesystem::path& projectPath,
    const std::string& sourceSetId,
    Stack::RawRecipe::RawEditAttributeBundle& bundle,
    std::string* errorMessage = nullptr);

// Applies one bundle to one existing project as a complete expected-revision
// transaction. Callers can run these sequentially on a background worker for
// a multi-selection without holding every project or asset in memory.
RawProjectAttributeTransferResult PasteRawEditAttributesIntoProject(
    const std::filesystem::path& projectPath,
    const std::string& sourceSetId,
    const Stack::RawRecipe::RawEditAttributeBundle& bundle,
    const std::vector<std::string>& selectedKeys);

} // namespace Stack::Project
