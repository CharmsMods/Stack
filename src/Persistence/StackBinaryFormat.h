#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>
#include "ThirdParty/json.hpp"

namespace StackBinaryFormat {

using json = nlohmann::json;

inline constexpr const char* kEditorProjectKind = "editor";
inline constexpr const char* kRenderProjectKind = "render";
inline constexpr const char* kCompositeProjectKind = "composite";

enum class FileKind : std::uint16_t {
    Project = 1,
    LibraryBundle = 2
};

struct ProjectLoadOptions {
    bool includeThumbnail = true;
    bool includeSourceImage = true;
    bool includePipelineData = true;
};

struct ProjectMetadata {
    std::string projectKind = kEditorProjectKind;
    std::string projectName;
    std::string timestamp;
    int sourceWidth = 0;
    int sourceHeight = 0;
};

struct ProjectDocument {
    ProjectMetadata metadata;
    std::vector<unsigned char> thumbnailBytes;
    std::vector<unsigned char> sourceImageBytes;
    json pipelineData = json();
};

struct BundledProjectDocument {
    std::string fileName;
    ProjectDocument project;
};

struct AssetDocument {
    std::string fileName;
    std::string displayName;
    std::string timestamp;
    std::string projectFileName;
    std::string projectName;
    int width = 0;
    int height = 0;
    std::vector<unsigned char> imageBytes;
};

struct LibraryBundleDocument {
    std::string bundleName = "Modular Studio Library";
    std::string timestamp;
    std::vector<BundledProjectDocument> projects;
    std::vector<AssetDocument> assets;
};

bool WriteProjectFile(const std::filesystem::path& path, const ProjectDocument& document);
bool ReadProjectFile(const std::filesystem::path& path, ProjectDocument& document, const ProjectLoadOptions& options = {});

bool WriteLibraryBundle(const std::filesystem::path& path, const LibraryBundleDocument& document);
bool ReadLibraryBundle(const std::filesystem::path& path, LibraryBundleDocument& document);

bool AreProjectsIdentical(const ProjectDocument& a, const ProjectDocument& b);

} // namespace StackBinaryFormat
