#pragma once
#include "Hdr/ImageFile.h"
#include <filesystem>
namespace Stack::Validation {
void WriteHdrRenditionComparison(const Hdr::Image& original,
    const std::vector<unsigned char>& hdrJpeg,const std::vector<unsigned char>& sdr,
    const std::filesystem::path& output);
}
