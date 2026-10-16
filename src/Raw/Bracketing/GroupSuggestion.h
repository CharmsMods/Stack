#pragma once

#include "Recipe.h"

namespace Raw::Bracketing {
struct CaptureExposure {
    std::string frameId;
    double exposureTimeSeconds = 0;
    double apertureFNumber = 0;
    double isoSpeed = 0;
};

// Input order breaks equal-exposure ties. This is the shared metadata policy
// used by saved-project creation and explicit selected-file bursts.
BracketingRecipe SuggestGroups(const std::vector<CaptureExposure>& captures);
} // namespace Raw::Bracketing
