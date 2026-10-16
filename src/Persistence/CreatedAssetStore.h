#pragma once
#include "ThirdParty/json.hpp"
#include <functional>
#include <string>
#include <vector>

namespace Stack::Project {
// Store adapters keep created data out of the bounded project manifest.
// The caller stages content through the existing project store transaction.
using CreatedAssetWriter = std::function<bool(const std::string& id, const std::string& name,
    const std::vector<unsigned char>& bytes, std::string& managedId)>;
using CreatedAssetReader = std::function<bool(const std::string& managedId, std::vector<unsigned char>& bytes)>;
bool ExternalizeCreatedAssets(nlohmann::json& pipeline, const CreatedAssetWriter& write,
    std::vector<std::string>& referenced);
bool RehydrateCreatedAssets(nlohmann::json& pipeline, const CreatedAssetReader& read);
} // namespace Stack::Project
