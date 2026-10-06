#pragma once
#include "Raw/Bracketing/Processor.h"

namespace Stack::Validation {
// Bounded diagnostic for same-exposure bursts. This is not a recipe mode.
void WriteBracketUpgradeComparison(const Raw::Bracketing::ProcessingRequest&,
    const Raw::Bracketing::BracketingResult&,const std::filesystem::path&,
    const std::vector<std::array<unsigned,2>>&);
}
