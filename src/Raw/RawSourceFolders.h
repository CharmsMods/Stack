#pragma once

#include "Raw/RawWorkspace.h"

namespace Stack::RawWorkspace {

// Windows path identity preserves Unicode, resolves available aliases and is
// case-insensitive. Unavailable folders remain registered, not silently lost.
std::string SourceFolderPathKey(const std::filesystem::path& path);
std::vector<std::filesystem::path> NormalizeSourceFolders(
    const std::vector<std::filesystem::path>& folders);
std::filesystem::path ResolveSourceWorkspaceRoot(
    const SourceRecord& source, const std::filesystem::path& fallback);
// Returns native source/membership identities and clears their aggregate-only
// backup keys. Idempotent; source ownership and cache paths remain unchanged.
SourceRecord NativeSourceRecord(const SourceRecord& source);
// Resolve only a real directory project stored below a validated
// <source>/Closet/Projects. Exports and arbitrary ancestors prove no owner.
// This performs filesystem reads and belongs at scan/open boundaries, not UI
// drawing. An empty result means that provenance was not established.
std::filesystem::path ResolveManagedProjectSourceFolder(
    const std::filesystem::path& projectPath);

// Produces a read-only aggregate projection. Each record retains its real
// owner, native relative path and existing cache paths. Project and membership
// projectId values are qualified by canonical project path; nativeProjectId
// retains their authored identity. Folder-card summaries remain native.
// The result deliberately
// has no layout: consumers must never persist it as one folder's catalog.
// One unavailable folder does not hide the others. A partial success reports
// its folder failures in errorMessage; cancellation returns no usable result.
ScanResult AggregateSourceFolders(
    const std::vector<std::filesystem::path>& folders,
    RawPathPredicate isRawPath = {},
    ScanProgressCallback progress = {},
    CancellationPredicate shouldCancel = {});

} // namespace Stack::RawWorkspace
