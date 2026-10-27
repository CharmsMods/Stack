#pragma once
#include "BracketingState.h"

namespace Stack::Project {

// Resolve and capture a native-detail owner only when submitting work. Completed
// pixels use their completed recipe; an owned, compatible interactive preview
// may instead inspect the proposed Standard recipe. No engine work is performed.
bool CaptureBracketingDetail(const BracketingState&, std::uint64_t inputRevision,
    BracketingState::DetailJob&, Raw::Bracketing::BracketingRecipe&, std::string& error);

// Call only on completion, not while drawing unchanged frames. Retaining a
// result keeps its resources alive but does not make an older request current.
bool CanAdoptBracketingDetail(const BracketingState&, const BracketingState::DetailJob&,
    std::uint64_t inputRevision);

} // namespace Stack::Project
