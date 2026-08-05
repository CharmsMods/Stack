#pragma once

#include <functional>
#include <string>
#include <vector>

namespace Stack::Restormer {

struct Tile {
    int x = 0;
    int y = 0;
    int width = 0;
    int height = 0;
};

struct TilePolicy {
    int size = 256;
    int overlap = 32;
};

struct TiledInferenceResult {
    bool ok = false;
    bool cancelled = false;
    std::string error;
    int completedTiles = 0;
    int totalTiles = 0;
};

std::vector<Tile> BuildTiles(
    int width,
    int height,
    const TilePolicy& policy);
int ReflectIndex(int coordinate, int size);
float RaisedCosineTileWeight(
    int coordinate,
    int tileExtent,
    int overlap,
    bool touchesLowerImageEdge,
    bool touchesUpperImageEdge);

using TileInferenceFunction = std::function<bool(
    const float* inputNchw,
    int tileWidth,
    int tileHeight,
    float* outputNchw,
    std::string& error)>;

TiledInferenceResult RunTiledInference(
    const std::vector<float>& inputInterleavedRgb,
    int width,
    int height,
    const TilePolicy& policy,
    const TileInferenceFunction& inferTile,
    const std::function<bool()>& shouldCancel,
    std::vector<float>& outInterleavedRgb);

} // namespace Stack::Restormer
