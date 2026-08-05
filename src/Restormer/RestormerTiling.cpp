#include "Restormer/RestormerTiling.h"

#include <algorithm>
#include <cmath>
#include <cstddef>

namespace Stack::Restormer {

std::vector<Tile> BuildTiles(
    int width,
    int height,
    const TilePolicy& requestedPolicy) {
    std::vector<Tile> tiles;
    if (width <= 0 || height <= 0) {
        return tiles;
    }
    const int size = std::max(8, requestedPolicy.size);
    const int overlap = std::clamp(requestedPolicy.overlap, 0, size - 8);
    const int step = std::max(8, size - overlap);
    auto positions = [size, step](int extent) {
        std::vector<int> result { 0 };
        while (result.back() + size < extent) {
            const int next = std::min(result.back() + step, extent - size);
            if (next <= result.back()) {
                break;
            }
            result.push_back(next);
        }
        return result;
    };
    const std::vector<int> xs = positions(width);
    const std::vector<int> ys = positions(height);
    tiles.reserve(xs.size() * ys.size());
    for (const int y : ys) {
        for (const int x : xs) {
            tiles.push_back({
                x,
                y,
                std::min(size, width - x),
                std::min(size, height - y)
            });
        }
    }
    return tiles;
}

int ReflectIndex(int coordinate, int size) {
    if (size <= 1) {
        return 0;
    }
    const int period = 2 * size - 2;
    int reflected = coordinate % period;
    if (reflected < 0) {
        reflected += period;
    }
    return reflected < size ? reflected : period - reflected;
}

float RaisedCosineTileWeight(
    int coordinate,
    int tileExtent,
    int overlap,
    bool touchesLowerImageEdge,
    bool touchesUpperImageEdge) {
    if (tileExtent <= 0 || overlap <= 0) {
        return 1.0f;
    }
    constexpr float pi = 3.14159265358979323846f;
    float weight = 1.0f;
    if (!touchesLowerImageEdge && coordinate < overlap) {
        const float t = std::clamp(
            (static_cast<float>(coordinate) + 0.5f) /
                static_cast<float>(overlap),
            0.0f,
            1.0f);
        weight *= 0.5f - 0.5f * std::cos(pi * t);
    }
    const int distanceToUpper = tileExtent - 1 - coordinate;
    if (!touchesUpperImageEdge && distanceToUpper < overlap) {
        const float t = std::clamp(
            (static_cast<float>(distanceToUpper) + 0.5f) /
                static_cast<float>(overlap),
            0.0f,
            1.0f);
        weight *= 0.5f - 0.5f * std::cos(pi * t);
    }
    return std::max(weight, 1.0e-6f);
}

TiledInferenceResult RunTiledInference(
    const std::vector<float>& inputInterleavedRgb,
    int width,
    int height,
    const TilePolicy& requestedPolicy,
    const TileInferenceFunction& inferTile,
    const std::function<bool()>& shouldCancel,
    std::vector<float>& outInterleavedRgb) {
    TiledInferenceResult result;
    const std::size_t pixelCount =
        width > 0 && height > 0
            ? static_cast<std::size_t>(width) *
                static_cast<std::size_t>(height)
            : 0U;
    if (pixelCount == 0U || inputInterleavedRgb.size() != pixelCount * 3U ||
        !inferTile) {
        result.error = "Tiled inference input is invalid.";
        return result;
    }

    TilePolicy policy = requestedPolicy;
    policy.size = std::max(8, policy.size);
    policy.overlap = std::clamp(policy.overlap, 0, policy.size - 8);
    const std::vector<Tile> tiles = BuildTiles(width, height, policy);
    result.totalTiles = static_cast<int>(tiles.size());
    std::vector<float> accumulation(pixelCount * 3U, 0.0f);
    std::vector<float> weights(pixelCount, 0.0f);

    for (const Tile& tile : tiles) {
        if (shouldCancel && shouldCancel()) {
            result.cancelled = true;
            result.error = "Restormer inference was cancelled.";
            return result;
        }
        const int paddedWidth = (tile.width + 7) & ~7;
        const int paddedHeight = (tile.height + 7) & ~7;
        const std::size_t paddedPixels =
            static_cast<std::size_t>(paddedWidth) *
            static_cast<std::size_t>(paddedHeight);
        std::vector<float> inputNchw(paddedPixels * 3U);
        std::vector<float> outputNchw(paddedPixels * 3U);
        for (int localY = 0; localY < paddedHeight; ++localY) {
            const int sourceY = tile.y + ReflectIndex(localY, tile.height);
            for (int localX = 0; localX < paddedWidth; ++localX) {
                const int sourceX = tile.x + ReflectIndex(localX, tile.width);
                const std::size_t source =
                    (static_cast<std::size_t>(sourceY) *
                         static_cast<std::size_t>(width) +
                     static_cast<std::size_t>(sourceX)) * 3U;
                const std::size_t destination =
                    static_cast<std::size_t>(localY) *
                        static_cast<std::size_t>(paddedWidth) +
                    static_cast<std::size_t>(localX);
                for (int channel = 0; channel < 3; ++channel) {
                    inputNchw[
                        static_cast<std::size_t>(channel) * paddedPixels +
                        destination] = inputInterleavedRgb[source + channel];
                }
            }
        }
        if (!inferTile(
                inputNchw.data(),
                paddedWidth,
                paddedHeight,
                outputNchw.data(),
                result.error)) {
            return result;
        }

        const bool leftEdge = tile.x == 0;
        const bool topEdge = tile.y == 0;
        const bool rightEdge = tile.x + tile.width >= width;
        const bool bottomEdge = tile.y + tile.height >= height;
        for (int localY = 0; localY < tile.height; ++localY) {
            const float weightY = RaisedCosineTileWeight(
                localY,
                tile.height,
                policy.overlap,
                topEdge,
                bottomEdge);
            for (int localX = 0; localX < tile.width; ++localX) {
                const float weight = weightY * RaisedCosineTileWeight(
                    localX,
                    tile.width,
                    policy.overlap,
                    leftEdge,
                    rightEdge);
                const std::size_t destination =
                    static_cast<std::size_t>(tile.y + localY) *
                        static_cast<std::size_t>(width) +
                    static_cast<std::size_t>(tile.x + localX);
                const std::size_t source =
                    static_cast<std::size_t>(localY) *
                        static_cast<std::size_t>(paddedWidth) +
                    static_cast<std::size_t>(localX);
                weights[destination] += weight;
                for (int channel = 0; channel < 3; ++channel) {
                    accumulation[destination * 3U + channel] +=
                        outputNchw[
                            static_cast<std::size_t>(channel) * paddedPixels +
                            source] * weight;
                }
            }
        }
        ++result.completedTiles;
    }

    outInterleavedRgb.resize(pixelCount * 3U);
    for (std::size_t pixel = 0; pixel < pixelCount; ++pixel) {
        const float inverseWeight =
            1.0f / std::max(weights[pixel], 1.0e-8f);
        for (int channel = 0; channel < 3; ++channel) {
            outInterleavedRgb[pixel * 3U + channel] =
                accumulation[pixel * 3U + channel] * inverseWeight;
        }
    }
    result.ok = true;
    return result;
}

} // namespace Stack::Restormer
