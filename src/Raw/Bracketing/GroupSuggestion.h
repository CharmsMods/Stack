#pragma once

#include "Recipe.h"

namespace Raw::Bracketing {
struct CaptureExposure {
    std::string frameId;
    double exposureTimeSeconds = 0;
    double apertureFNumber = 0;
    double isoSpeed = 0;
};

// Input order breaks equal-exposure ties. Metadata is an editable UI grouping
// hint shared by project creation and selected-file bursts, never a calibrated
// gain or an admission requirement. Invalid tags behave like missing tags.
BracketingRecipe SuggestGroups(const std::vector<CaptureExposure>& captures);
} // namespace Raw::Bracketing
