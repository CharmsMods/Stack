#pragma once
#include "RawProjectModel.h"
#include "Raw/Bracketing/Recipe.h"

namespace Stack::Project {
inline bool IsBracketing(const MultiFrameSourceSet& set) {return set.settings.contains("bracketing");}
Raw::Bracketing::BracketingRecipe SuggestBracketingGroups(const RawProjectSnapshot&,const MultiFrameSourceSet&);
void InitializeBracketing(MultiFrameSourceSet&,const RawProjectSnapshot&);
} // namespace Stack::Project
