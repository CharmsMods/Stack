#pragma once

#include "Persistence/StackBinaryFormat.h"

#include <filesystem>
#include <string>
#include <vector>

namespace Stack::Library::ImageHelpers {

std::vector<unsigned char> ReadBinaryJsonBytes(const nlohmann::json& value);

bool ExtractEmbeddedGraphSourcePng(const nlohmann::json& pipelineData, std::vector<unsigned char>& outPngBytes);

bool HasMeaningfulPixels(const std::vector<unsigned char>& pixels);

std::vector<unsigned char> ResizePixelsNearest(
    const std::vector<unsigned char>& sourcePixels,
    int sourceWidth,
    int sourceHeight,
    int& outWidth,
    int& outHeight,
    int maxDimension = 400);

bool LoadRgbaImageFromFile(const std::filesystem::path& path, std::vector<unsigned char>& outPixels, int& outW, int& outH);

bool ReadImageInfo(const std::filesystem::path& path, int& outW, int& outH, int& outChannels);

bool DecodeImageBytes(
    const std::vector<unsigned char>& encodedImage,
    std::vector<unsigned char>& outPixels,
    int& outW,
    int& outH,
    int& outChannels);

void FlipImageRowsInPlace(std::vector<unsigned char>& pixels, int width, int height, int channels = 4);

bool DecodePreviewBytes(
    const std::vector<unsigned char>& encodedBytes,
    std::vector<unsigned char>& outPixels,
    int& outW,
    int& outH);

bool ResolveProjectPreviewPixels(
    const StackBinaryFormat::ProjectDocument& project,
    const std::filesystem::path* fallbackAssetPath,
    const std::vector<unsigned char>* fallbackAssetBytes,
    std::vector<unsigned char>& outPixels,
    int& outW,
    int& outH,
    std::string& outStatus);

} // namespace Stack::Library::ImageHelpers
