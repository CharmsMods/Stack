#pragma once
#include <cstdint>
#include <filesystem>
#include <functional>
#include <istream>

namespace Stack::Project::BracketAuxiliaryStorage {
void Compress(const std::filesystem::path& source, const std::filesystem::path& destination);
void Read(const std::filesystem::path& file, const std::function<void(std::istream&)>& consume);
}
