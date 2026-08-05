#include "Editor/GraphCapture.h"

#include "Editor/NodeGraph/EditorNodeGraph.h"
#include "ThirdParty/stb_image_write.h"
#include "Utils/PngEncodingUtils.h"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <limits>
#include <new>
#include <stdexcept>

namespace Stack::EditorGraphCapture {
namespace {

constexpr int kDefaultLongEdge = 7680;

int RoundedPositiveDimension(double value) {
    if (!std::isfinite(value)) {
        return 1;
    }
    if (value <= 1.0) {
        return 1;
    }
    if (value >= static_cast<double>(std::numeric_limits<int>::max())) {
        return std::numeric_limits<int>::max();
    }
    return static_cast<int>(std::llround(value));
}

struct WriteContext {
    std::vector<unsigned char>* bytes = nullptr;
    bool failed = false;
};

void WriteToVector(void* context, void* data, int size) {
    auto* writeContext = static_cast<WriteContext*>(context);
    if (!writeContext || !writeContext->bytes ||
        writeContext->failed || !data || size <= 0) {
        return;
    }
    const auto* begin = static_cast<const unsigned char*>(data);
    try {
        writeContext->bytes->insert(
            writeContext->bytes->end(),
            begin,
            begin + size);
    } catch (const std::bad_alloc&) {
        writeContext->failed = true;
    } catch (const std::length_error&) {
        writeContext->failed = true;
    }
}

bool HasExpectedRgbaSize(const std::vector<unsigned char>& rgba, int width, int height) {
    if (width <= 0 || height <= 0) {
        return false;
    }
    const std::uint64_t expected =
        static_cast<std::uint64_t>(width) * static_cast<std::uint64_t>(height) * 4ull;
    return expected <= static_cast<std::uint64_t>(std::numeric_limits<std::size_t>::max()) &&
        rgba.size() == static_cast<std::size_t>(expected);
}

} // namespace

void InitializeEightKLongEdge(Settings& settings, float aspectRatio) {
    const float safeAspect = std::isfinite(aspectRatio) && aspectRatio > 0.0001f
        ? aspectRatio
        : 1.0f;
    if (safeAspect >= 1.0f) {
        settings.resolutionDriver = ResolutionDriver::Width;
        settings.width = kDefaultLongEdge;
        settings.height = RoundedPositiveDimension(static_cast<double>(settings.width) / safeAspect);
    } else {
        settings.resolutionDriver = ResolutionDriver::Height;
        settings.height = kDefaultLongEdge;
        settings.width = RoundedPositiveDimension(static_cast<double>(settings.height) * safeAspect);
    }
    settings.resolutionInitialized = true;
}

void ResolveLinkedResolution(Settings& settings, float aspectRatio) {
    const float safeAspect = std::isfinite(aspectRatio) && aspectRatio > 0.0001f
        ? aspectRatio
        : 1.0f;
    if (!settings.resolutionInitialized) {
        InitializeEightKLongEdge(settings, safeAspect);
        return;
    }

    if (settings.resolutionDriver == ResolutionDriver::Width) {
        settings.width = std::max(1, settings.width);
        settings.height = RoundedPositiveDimension(static_cast<double>(settings.width) / safeAspect);
    } else {
        settings.height = std::max(1, settings.height);
        settings.width = RoundedPositiveDimension(static_cast<double>(settings.height) * safeAspect);
    }
}

void ApplyNodeStatePreset(EditorNodeGraph::Graph& graph, NodeState nodeState) {
    if (nodeState == NodeState::AsShown) {
        return;
    }
    const bool expanded = nodeState == NodeState::ExpandAll;
    for (EditorNodeGraph::Node& node : graph.GetNodes()) {
        node.expanded = expanded;
    }
}

bool ValidateResolution(
    int width,
    int height,
    const ResolutionLimits& limits,
    std::string* errorMessage) {
    auto fail = [&](const std::string& message) {
        if (errorMessage) {
            *errorMessage = message;
        }
        return false;
    };

    if (width <= 0 || height <= 0) {
        return fail("Width and height must both be positive.");
    }
    if (width > limits.maxEdge || height > limits.maxEdge) {
        return fail("The requested dimensions exceed this GPU's capture edge limit.");
    }
    if (std::min(width, height) < limits.minDerivedEdge) {
        return fail("The derived image edge is too small for a useful graph capture.");
    }
    const std::uint64_t pixels =
        static_cast<std::uint64_t>(width) * static_cast<std::uint64_t>(height);
    if (pixels > limits.maxPixels) {
        return fail("The requested image exceeds the 100-megapixel capture limit.");
    }
    if (pixels > static_cast<std::uint64_t>(std::numeric_limits<std::size_t>::max() / 4u)) {
        return fail("The requested image is too large for addressable memory.");
    }
    if (errorMessage) {
        errorMessage->clear();
    }
    return true;
}

CameraTransform FitBoundsToCanvas(
    const FloatBounds& bounds,
    float canvasWidth,
    float canvasHeight,
    float paddingPercent,
    float minZoom,
    float maxZoom) {
    CameraTransform transform;
    if (!bounds.valid ||
        !std::isfinite(bounds.minX) || !std::isfinite(bounds.minY) ||
        !std::isfinite(bounds.maxX) || !std::isfinite(bounds.maxY) ||
        canvasWidth <= 0.0f || canvasHeight <= 0.0f) {
        return transform;
    }

    const float graphWidth = std::max(1.0f, bounds.maxX - bounds.minX);
    const float graphHeight = std::max(1.0f, bounds.maxY - bounds.minY);
    const float safePaddingPercent = std::clamp(paddingPercent, 0.0f, 25.0f);
    const float padding = std::min(canvasWidth, canvasHeight) * safePaddingPercent * 0.01f;
    const float usableWidth = std::max(1.0f, canvasWidth - padding * 2.0f);
    const float usableHeight = std::max(1.0f, canvasHeight - padding * 2.0f);
    const float safeMinZoom = std::max(0.0001f, std::min(minZoom, maxZoom));
    const float safeMaxZoom = std::max(safeMinZoom, maxZoom);
    transform.zoom = std::clamp(
        std::min(usableWidth / graphWidth, usableHeight / graphHeight),
        safeMinZoom,
        safeMaxZoom);
    transform.panX =
        ((canvasWidth - graphWidth * transform.zoom) * 0.5f) - bounds.minX * transform.zoom;
    transform.panY =
        ((canvasHeight - graphHeight * transform.zoom) * 0.5f) - bounds.minY * transform.zoom;
    return transform;
}

bool NormalizeReadbackRgba(
    std::vector<unsigned char>& pixels,
    int width,
    int height,
    bool unpremultiplyAlpha) {
    if (!HasExpectedRgbaSize(pixels, width, height)) {
        return false;
    }

    if (!Stack::PixelBuffer::FlipInterleavedRowsInPlace(
            pixels, width, height, 4)) {
        return false;
    }

    if (unpremultiplyAlpha) {
        for (std::size_t index = 0; index + 3 < pixels.size(); index += 4) {
            const unsigned int alpha = pixels[index + 3];
            if (alpha == 0) {
                pixels[index + 0] = 0;
                pixels[index + 1] = 0;
                pixels[index + 2] = 0;
            } else if (alpha < 255) {
                for (int channel = 0; channel < 3; ++channel) {
                    const unsigned int value = pixels[index + static_cast<std::size_t>(channel)];
                    pixels[index + static_cast<std::size_t>(channel)] = static_cast<unsigned char>(
                        std::min(255u, (value * 255u + alpha / 2u) / alpha));
                }
            }
        }
    }
    return true;
}

bool EncodePng(
    const std::vector<unsigned char>& rgba,
    int width,
    int height,
    std::vector<unsigned char>& encoded) {
    encoded.clear();
    if (!HasExpectedRgbaSize(rgba, width, height)) {
        return false;
    }
    encoded = Stack::PngEncoding::EncodeInterleaved(
        rgba, width, height, 4);
    return !encoded.empty();
}

bool EncodeBmp(
    const std::vector<unsigned char>& rgba,
    int width,
    int height,
    std::vector<unsigned char>& encoded) {
    encoded.clear();
    if (!HasExpectedRgbaSize(rgba, width, height)) {
        return false;
    }
    WriteContext context { &encoded, false };
    const int result = stbi_write_bmp_to_func(
        WriteToVector,
        &context,
        width,
        height,
        4,
        rgba.data());
    if (result == 0 || context.failed) {
        encoded.clear();
        return false;
    }
    return !encoded.empty();
}

bool WriteEncodedFile(
    const std::string& path,
    const std::vector<unsigned char>& encoded,
    std::string* errorMessage) {
    if (path.empty() || encoded.empty()) {
        if (errorMessage) {
            *errorMessage = "The encoded graph image is empty.";
        }
        return false;
    }

    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    if (!output.is_open()) {
        if (errorMessage) {
            *errorMessage = "Could not open the selected graph image path for writing.";
        }
        return false;
    }
    output.write(
        reinterpret_cast<const char*>(encoded.data()),
        static_cast<std::streamsize>(encoded.size()));
    output.flush();
    if (!output.good()) {
        if (errorMessage) {
            *errorMessage = "Writing the graph image did not complete successfully.";
        }
        return false;
    }
    output.close();

    std::error_code ec;
    const std::uintmax_t writtenSize = std::filesystem::file_size(path, ec);
    if (ec || writtenSize != encoded.size()) {
        if (errorMessage) {
            *errorMessage = "The saved graph image could not be verified.";
        }
        return false;
    }
    if (errorMessage) {
        errorMessage->clear();
    }
    return true;
}

} // namespace Stack::EditorGraphCapture
