#pragma once

#include "Raw/RawImageData.h"

#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>

namespace Stack::EditorMultiFrameCache {

// Stores only the graph's rebuildable final Virtual Bayer output. Project
// structure, recipes, and embedded originals remain authoritative elsewhere.
bool WriteBurstResult(
    const std::filesystem::path& path,
    const std::string& graphIdentity,
    std::uint64_t inputRevision,
    const Raw::RawImageData& raw,
    std::string* errorMessage = nullptr);

bool ReadBurstResult(
    const std::filesystem::path& path,
    const std::string& expectedGraphIdentity,
    std::uint64_t expectedInputRevision,
    std::shared_ptr<Raw::RawImageData>& raw,
    std::string* errorMessage = nullptr);

} // namespace Stack::EditorMultiFrameCache
