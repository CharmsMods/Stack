#pragma once

#include "Utils/AtomicExportFile.h"
#include <cstddef>

namespace Stack::Library {
struct ImportProjectDestination {
    std::filesystem::path path;
    bool deferred = false;
};
struct ImportAssetDestination {
    std::filesystem::path path;
    const std::vector<unsigned char>* bytes = nullptr;
    bool deferred = false;
    // Only the existing project's own preview may await its Replace choice.
    bool projectPreviewReplacement = false;
};
struct PlannedAssetImport {
    std::size_t index = 0;
    FileSave::ExportFileTarget target;
    bool reuse = false;
    bool deferred = false;
};
struct LibraryImportPlan {
    std::vector<PlannedAssetImport> assets;
};

// Read-only preflight. Inputs have already passed the Library's filename
// normalization boundary. Failure leaves both the filesystem and plan alone.
bool PrepareLibraryImportPlan(
    const std::vector<ImportProjectDestination>& projects,
    const std::vector<ImportAssetDestination>& assets,
    LibraryImportPlan& plan, std::string& error);
} // namespace Stack::Library
