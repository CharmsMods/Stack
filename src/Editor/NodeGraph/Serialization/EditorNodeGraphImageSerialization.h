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
bool DecodeImagePayloadPngBytes(const std::vector<unsigned char>& pngBytes, ImagePayload& payload);
std::vector<unsigned char> ReadBinaryJsonBytes(const nlohmann::json& value);

} // namespace EditorNodeGraph
