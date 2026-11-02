#pragma once
#include "Raw/RawImageData.h"
#include "Raw/DepthMask.h"
#include "ThirdParty/json.hpp"
#include <filesystem>

namespace Stack::Project::DngMaster {
void Write(const std::filesystem::path& path, const Raw::RawImageData& raw,
    const nlohmann::json& marker);
std::vector<Raw::DepthMask> ReadMasks(const std::filesystem::path& path, int* orientation = nullptr);
nlohmann::json ReadMetadata(const std::filesystem::path& path);
std::vector<float> Read(const std::filesystem::path& path, int width, int height,
    const std::string& expectedResultId);
}
