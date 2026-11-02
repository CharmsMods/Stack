#pragma once
#include <filesystem>
#include <string>
#include <vector>

namespace Stack::Project {
struct ClosetMergePlan {
    std::filesystem::path parentRoot;
    std::filesystem::path nestedCloset;
    std::vector<std::filesystem::path> projects;
    std::vector<std::string> revisions;
    std::vector<std::string> authoredAssets;
};
bool InspectClosetMerge(const std::filesystem::path& parentRoot,
    const std::filesystem::path& nestedCloset, ClosetMergePlan& plan, std::string& error);
bool MergeCloset(const ClosetMergePlan& plan, std::string& error);
}
