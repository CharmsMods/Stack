#pragma once
#include "ThirdParty/json.hpp"
#include "Raw/DepthMask.h"
#include <filesystem>
#include <vector>

namespace Stack::Project::RgbMaster {
void Write(const std::filesystem::path& path, int width, int height,
    const std::vector<float>& rgb, const nlohmann::json& metadata,const std::vector<Raw::DepthMask>& masks={});
std::vector<Raw::DepthMask> ReadMasks(const std::filesystem::path&,const nlohmann::json& metadata);
nlohmann::json ReadMetadata(const std::filesystem::path& path);
std::vector<float> Read(const std::filesystem::path& path, int width, int height,
    const std::string& expectedResultId);
}
