#pragma once

#include "Persistence/ProjectStore.h"
#include "Raw/Bracketing/Processor.h"

namespace Stack::Project {

bool StageBracketingResult(const ProjectStoreHandle& store, const ProjectStoreTransaction& transaction,
    RawProjectSnapshot& snapshot, const std::string& setId,
    const Raw::Bracketing::BracketingResult& result, const std::vector<unsigned char>& cover, std::string& error);
bool SaveBracketingResult(const ProjectStoreHandle& store, RawProjectSnapshot& snapshot,
    const std::string& setId, const Raw::Bracketing::BracketingResult& result,
    const std::vector<unsigned char>& cover, std::string& error);
bool RestoreBracketingResult(const ProjectStoreHandle& store, const RawProjectSnapshot& snapshot,
    const std::string& setId, Raw::Bracketing::BracketingResult& result, std::string& error,
    const std::function<bool()>& shouldCancel = {});
bool HasSavedBracketingResult(const RawProjectSnapshot&, const MultiFrameSourceSet&);

} // namespace Stack::Project
