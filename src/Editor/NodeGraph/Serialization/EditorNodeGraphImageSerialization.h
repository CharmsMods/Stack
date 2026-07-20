#pragma once

#include "Editor/NodeGraph/EditorNodeGraph.h"
#include "ThirdParty/json.hpp"

#include <vector>

namespace EditorNodeGraph {

std::vector<unsigned char> EncodeImagePayloadPngForStorage(
    const std::vector<unsigned char>& bottomLeftPixels,
    int width,
    int height,
    int channels);
void BuildImagePayloadPreview(
    const std::vector<unsigned char>& bottomLeftPixels,
    int width,
    int height,
    int channels,
    std::vector<unsigned char>& outPixels,
    int& outWidth,
    int& outHeight,
    int& outChannels,
    int maxDimension = 768);
bool DecodeImagePayloadPngBytes(const std::vector<unsigned char>& pngBytes, ImagePayload& payload);
Stack::NodeMath::SourceColorMetadata InspectImageFileColorMetadata(
    const std::string& path,
    int width,
    int height,
    int originalChannels);
std::vector<unsigned char> ReadBinaryJsonBytes(const nlohmann::json& value);

} // namespace EditorNodeGraph
