#pragma once

#include <filesystem>
#include <string>
#include "ThirdParty/json.hpp"

namespace Raw::Bracketing {

// Optional, immutable motion evidence supplied to the shared engine. Files are
// interleaved little-endian float32 dx,dy in proposal pixels. Both directions
// have the same extent; a pixel center maps to originRaw + index * stepRaw.
// These are search seeds, never pixels, confidence, or an accepted warp.
struct AlignmentProposal {
    std::string referenceFrameId, sourceFrameId;
    std::string referenceSha256, sourceSha256, modelIdentity;
    unsigned width=0, height=0, rawWidth=0, rawHeight=0;
    double originRaw=0, stepRaw=0;
    std::filesystem::path forwardPath, reversePath;
    std::string forwardSha256, reverseSha256;
};

// Locations are transport details. Geometry, sources, model/preprocessing and
// exact field content are the identity, including for saved result adoption.
inline nlohmann::json AlignmentProposalIdentity(const AlignmentProposal& p) {
    return {{"referenceFrameId",p.referenceFrameId},{"sourceFrameId",p.sourceFrameId},
        {"referenceSha256",p.referenceSha256},{"sourceSha256",p.sourceSha256},
        {"modelIdentity",p.modelIdentity},{"width",p.width},{"height",p.height},
        {"rawWidth",p.rawWidth},{"rawHeight",p.rawHeight},
        {"originRaw",p.originRaw},{"stepRaw",p.stepRaw},
        {"forwardSha256",p.forwardSha256},{"reverseSha256",p.reverseSha256}};
}

} // namespace Raw::Bracketing
