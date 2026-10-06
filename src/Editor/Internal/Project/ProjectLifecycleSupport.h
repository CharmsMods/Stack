#pragma once

#include "Persistence/StackBinaryFormat.h"
#include "Raw/RawWorkspace.h"

#include <cstdint>
#include <filesystem>
#include <string>

namespace Stack::Editor::ProjectInternal {

std::int64_t RawWorkspaceProjectFileTimeTicks(
    const std::filesystem::path& path);

void EnsureMinimalProjectDocument(
    StackBinaryFormat::ProjectDocument& document,
    const std::string& name);

void OverlayOwnedJsonFields(
    nlohmann::json& current,
    const nlohmann::json& owned);

void ApplyRawWorkspaceProjectInfoToSource(
    RawWorkspace::SourceRecord& source,
    const StackBinaryFormat::ProjectDocument& document,
    const std::filesystem::path& projectPath,
    const std::filesystem::path& projectRelativePath,
    bool autosaved,
    bool dirty,
    std::string associationReason);

} // namespace Stack::Editor::ProjectInternal
