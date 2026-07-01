#pragma once

#include "Persistence/StackBinaryFormat.h"

#include <cstdint>
#include <ctime>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

struct ProjectEntry;

namespace Stack::Library::StorageHelpers {

std::string BuildTimestampString(std::time_t now);
std::string BuildTimestampString();
std::string BuildTimestampStringFromFileTime(const std::filesystem::file_time_type& writeTime);

std::string TrimWhitespace(const std::string& value);
std::string SanitizeFileStem(const std::string& value);
std::string DisplayNameFromStem(const std::string& stem);

bool IsSupportedAssetExtension(const std::filesystem::path& path);
bool IsSupportedProjectExtension(const std::filesystem::path& path);
bool IsSupportedAssetMetadataExtension(const std::filesystem::path& path);

std::string EnsureProjectFileNameForKind(
    const std::string& fileName,
    const std::string& fallbackStem,
    const std::string& projectKind);
std::string EnsureProjectFileName(const std::string& fileName, const std::string& fallbackName);
std::string EnsureAssetFileName(const std::string& fileName, const std::string& fallbackName);

std::string ResolveAssetProjectFileName(const std::string& stem, const std::vector<std::shared_ptr<ProjectEntry>>& projects);
std::string ResolveAssetProjectFileName(const std::string& stem, const std::vector<StackBinaryFormat::BundledProjectDocument>& projects);

std::uint64_t ComputeExactPixelFingerprint(const std::vector<unsigned char>& pixels);
std::uint64_t ComputeAverageHash64(const std::vector<unsigned char>& pixels, int width, int height);
int CountHammingDistance64(std::uint64_t a, std::uint64_t b);
float ComputeNameSimilarityScore(const std::string& a, const std::string& b);
bool ShouldQueueAssetConflict(
    const std::string& localName,
    const std::vector<unsigned char>& localPixels,
    int localWidth,
    int localHeight,
    const std::string& importedName,
    const std::vector<unsigned char>& importedPixels,
    int importedWidth,
    int importedHeight,
    bool& outExactMatch);

bool ReadFileBytes(const std::filesystem::path& path, std::vector<unsigned char>& outBytes);
bool WriteFileBytes(const std::filesystem::path& path, const std::vector<unsigned char>& bytes);

bool LoadLegacyProjectDocument(
    const std::filesystem::path& path,
    StackBinaryFormat::ProjectDocument& outDocument,
    const StackBinaryFormat::ProjectLoadOptions& options);

std::vector<unsigned char> DecodeDataUrl(const std::string& dataUrl);
std::string ComputeImageHash(const std::vector<unsigned char>& data);

} // namespace Stack::Library::StorageHelpers
