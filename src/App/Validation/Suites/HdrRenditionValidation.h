#pragma once
#include <filesystem>
namespace Stack::Validation {
// A bounded check of a supplied, already-edited HDR/SDR pair. Inputs stay read-only.
void ValidateHdrRenditionPair(const std::filesystem::path& hdr,
    const std::filesystem::path& sdr,const std::filesystem::path& output);
}
