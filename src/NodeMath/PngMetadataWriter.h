#pragma once

#include "NodeMath/ContractTypes.h"

#include <vector>

namespace Stack::NodeMath {

struct PngColorMetadataChunks {
    bool writeSrgb = false;
    bool writeDisplayP3Cicp = false;
    // Exact PNG iCCP chunk payload: profile name, compression method, and
    // compressed profile bytes. Empty means no retained PNG profile is available.
    std::vector<unsigned char> retainedIccpPayload;
};

std::vector<unsigned char> InsertPngColorMetadataChunks(
    const std::vector<unsigned char>& png,
    const PngColorMetadataChunks& chunks,
    std::vector<ContractIssue>& issues);

} // namespace Stack::NodeMath
