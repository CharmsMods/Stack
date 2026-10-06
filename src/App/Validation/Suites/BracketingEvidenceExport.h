#pragma once
#include "Raw/Bracketing/Processor.h"

namespace Stack::Validation {
void WriteBracketEvidence(const Raw::Bracketing::ProcessingRequest&,
    const Raw::Bracketing::BracketingResult&,const std::filesystem::path&,
    const std::vector<std::array<unsigned,2>>&);
}
