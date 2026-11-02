#pragma once
#include <filesystem>
#include <string>
#include <vector>

namespace Stack::RawWorkspace {
// A portable copy can retain the same document ID as its working project.
inline std::string ProjectCoverKey(const std::filesystem::path& projectPath,const std::string& projectId) {
    return projectPath.empty() ? "project-overlay:id:"+projectId :
        "project-overlay:path:"+projectPath.lexically_normal().generic_u8string();
}
std::filesystem::path WriteProjectCoverCache(const std::filesystem::path& directory,
    const std::string& projectId, const std::vector<unsigned char>& bytes);
}
