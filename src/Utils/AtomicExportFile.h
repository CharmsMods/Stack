#pragma once

#include "Utils/SavePathConfirmation.h"
#include <filesystem>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace Stack::FileSave {

// Captured on the initiating thread, before rendering/encoding. The writer
// must not infer permission from whatever file happens to exist later.
struct ExportFileTarget {
    TargetApproval approval;
    std::uint64_t deviceId = 0;
    std::uint64_t fileId = 0;
    std::vector<std::filesystem::path> protectedSources;
};

bool PrepareExportFileTarget(
    const std::filesystem::path& destination,
    const std::vector<std::filesystem::path>& protectedSources,
    const std::vector<std::string>& allowedExtensions,
    ExportFileTarget& target,
    std::string& error);

// Stages beside the destination in an exclusively claimed directory. Failed
// writes never truncate the existing destination or remove another export's
// staging file. Concurrent in-process publishers recheck under one lock.
bool WriteExportFileAtomically(
    const ExportFileTarget& target,
    const std::vector<unsigned char>& bytes,
    std::string& error,
    const std::function<bool()>& isCanceled = {});

} // namespace Stack::FileSave
