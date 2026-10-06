#pragma once
#include "CreamPalette.h"
#include "ThirdParty/json.hpp"
#include <filesystem>
#include <string>
namespace StackAppearance {
nlohmann::json EncodeCreamPalette(const CreamPalette& palette);
bool DecodeCreamPalette(const nlohmann::json& value, CreamPalette& palette);
bool WriteAppearanceAtomically(const std::filesystem::path& path, const nlohmann::json& value,
    std::string* errorMessage = nullptr);
}
