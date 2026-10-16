#pragma once
#include "Project/BracketingState.h"
#include "Persistence/ProjectStore.h"

namespace Stack::Project {
struct BracketingCopy {
    std::vector<BracketingDraftSource> sources;
    Raw::Bracketing::BracketingRecipe recipe;
    std::string setId, projectName;
    bool metadataPending = false;
};
bool StageBracketingCopy(const BracketingCopy& copy, const ProjectStoreHandle& store,
    const ProjectStoreTransaction& transaction, RawProjectSnapshot& snapshot, std::string& error);
}
