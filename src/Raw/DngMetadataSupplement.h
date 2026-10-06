#pragma once

#include <filesystem>
#include <functional>

namespace Raw {
struct RawMetadata;

// Adds metadata LibRaw does not expose. Returns false only on cancellation.
bool ApplyDngSupplement(const std::filesystem::path& path, RawMetadata& metadata,
    const std::function<bool()>& shouldCancel = {});
} // namespace Raw
