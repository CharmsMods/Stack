#pragma once
#include <filesystem>
#include <string>
#include <vector>
namespace Stack::Library {
// Preserve every existing file. Late collisions retry with a fresh filename;
// observed existing files are never converted into overwrite approval.
bool WriteNewLibraryAsset(const std::filesystem::path& folder,
    const std::filesystem::path& preferredFileName, const std::vector<unsigned char>& bytes,
    std::filesystem::path& publishedPath, std::string& error);
}
