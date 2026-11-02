#pragma once
#include "Persistence/ProjectStore.h"
#include "Raw/Bracketing/Processor.h"

namespace Stack::Project {
nlohmann::json BracketMasterMetadata(const std::filesystem::path& path);
std::filesystem::path PublishBracketMaster(const ProjectStoreHandle& store, const RawProjectSnapshot& project,
    const MultiFrameSourceSet& set, const Raw::Bracketing::BracketingResult& result);
std::shared_ptr<const Raw::RawImageData> ReadBracketMasterMaskSource(const std::filesystem::path& path);
void ReadBracketMasterPixels(const std::filesystem::path& path, const std::string& resultId, Raw::RawImageData& raw);
}
