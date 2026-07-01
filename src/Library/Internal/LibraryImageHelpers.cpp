#include "Library/Internal/LibraryImageHelpers.h"

#include "Editor/EditorModule.h"
#include "ThirdParty/stb_image.h"

#include <algorithm>
#include <cstring>
#include <utility>

namespace Stack::Library::ImageHelpers {

namespace StackFormat = StackBinaryFormat;

std::vector<unsigned char> ReadBinaryJsonBytes(const nlohmann::json& value) {
    if (!value.is_binary()) {
        return {};
    }
    const auto& binaryValue = value.get_binary();
    return std::vector<unsigned char>(binaryValue.begin(), binaryValue.end());
}

bool ExtractEmbeddedGraphSourcePng(const nlohmann::json& pipelineData, std::vector<unsigned char>& outPngBytes) {
    outPngBytes.clear();
    if (!pipelineData.is_object()) {
        return false;
    }

    const nlohmann::json graphJson = pipelineData.value("nodeGraph", nlohmann::json::object());
    if (!graphJson.is_object()) {
        return false;
    }

    const int activeImageNodeId = graphJson.value("activeImageNodeId", 0);
    const nlohmann::json nodes = graphJson.value("nodes", nlohmann::json::array());
    if (!nodes.is_array()) {
        return false;
    }

    auto readImageNodePng = [&](const nlohmann::json& item) {
        if (!item.is_object() ||
            item.value("kind", std::string()) != "Image" ||
            !item.contains("pngBytes")) {
            return false;
        }
        outPngBytes = ReadBinaryJsonBytes(item.at("pngBytes"));
        return !outPngBytes.empty();
    };

    if (activeImageNodeId > 0) {
        for (const nlohmann::json& item : nodes) {
            if (item.value("id", 0) == activeImageNodeId && readImageNodePng(item)) {
                return true;
            }
        }
    }

    for (const nlohmann::json& item : nodes) {
        if (readImageNodePng(item)) {
            return true;
        }
    }

    outPngBytes.clear();
    return false;
}

bool HasMeaningfulPixels(const std::vector<unsigned char>& pixels) {
    for (std::size_t index = 0; index + 3 < pixels.size(); index += 4) {
        if (pixels[index + 3] > 4) {
            return true;
        }

        if (pixels[index] > 4 || pixels[index + 1] > 4 || pixels[index + 2] > 4) {
            return true;
        }
    }
    return false;
}

std::vector<unsigned char> ResizePixelsNearest(
    const std::vector<unsigned char>& sourcePixels,
    int sourceWidth,
    int sourceHeight,
    int& outWidth,
    int& outHeight,
    int maxDimension) {

    outWidth = sourceWidth;
    outHeight = sourceHeight;

    if (sourcePixels.empty() || sourceWidth <= 0 || sourceHeight <= 0) {
        return {};
    }

    if (sourceWidth <= maxDimension && sourceHeight <= maxDimension) {
        return sourcePixels;
    }

    if (sourceWidth > sourceHeight) {
        outWidth = maxDimension;
        outHeight = static_cast<int>((static_cast<float>(sourceHeight) / static_cast<float>(sourceWidth)) * outWidth);
    } else {
        outHeight = maxDimension;
        outWidth = static_cast<int>((static_cast<float>(sourceWidth) / static_cast<float>(sourceHeight)) * outHeight);
    }

    outWidth = std::max(outWidth, 1);
    outHeight = std::max(outHeight, 1);

    std::vector<unsigned char> resizedPixels(outWidth * outHeight * 4);
    for (int y = 0; y < outHeight; ++y) {
        for (int x = 0; x < outWidth; ++x) {
            const int srcX = (x * sourceWidth) / outWidth;
            const int srcY = (y * sourceHeight) / outHeight;
            const int srcIdx = (srcY * sourceWidth + srcX) * 4;
            const int dstIdx = (y * outWidth + x) * 4;
            for (int channel = 0; channel < 4; ++channel) {
                resizedPixels[dstIdx + channel] = sourcePixels[srcIdx + channel];
            }
        }
    }

    return resizedPixels;
}

bool LoadRgbaImageFromFile(const std::filesystem::path& path, std::vector<unsigned char>& outPixels, int& outW, int& outH) {
    outPixels.clear();
    outW = 0;
    outH = 0;

    if (!std::filesystem::exists(path)) {
        return false;
    }

    int channels = 0;
    stbi_set_flip_vertically_on_load_thread(1);
    unsigned char* pixels = stbi_load(path.string().c_str(), &outW, &outH, &channels, 4);
    if (!pixels) {
        return false;
    }

    outPixels.assign(pixels, pixels + (outW * outH * 4));
    stbi_image_free(pixels);
    return true;
}

bool ReadImageInfo(const std::filesystem::path& path, int& outW, int& outH, int& outChannels) {
    outW = 0;
    outH = 0;
    outChannels = 0;
    return stbi_info(path.string().c_str(), &outW, &outH, &outChannels) != 0;
}

bool DecodeImageBytes(
    const std::vector<unsigned char>& encodedImage,
    std::vector<unsigned char>& outPixels,
    int& outW,
    int& outH,
    int& outChannels) {
    outPixels.clear();
    outW = 0;
    outH = 0;
    outChannels = 0;
    if (encodedImage.empty()) {
        return false;
    }

    int channels = 0;
    stbi_set_flip_vertically_on_load_thread(1);
    unsigned char* pixels = stbi_load_from_memory(
        encodedImage.data(),
        static_cast<int>(encodedImage.size()),
        &outW,
        &outH,
        &channels,
        4);
    if (!pixels) {
        return false;
    }

    outChannels = 4;
    outPixels.assign(pixels, pixels + (outW * outH * 4));
    stbi_image_free(pixels);
    return true;
}

void FlipImageRowsInPlace(std::vector<unsigned char>& pixels, int width, int height, int channels) {
    if (pixels.empty() || width <= 0 || height <= 0 || channels <= 0) return;
    const std::size_t rowSize = static_cast<std::size_t>(width * channels);
    std::vector<unsigned char> temp(rowSize);
    for (int y = 0; y < height / 2; ++y) {
        unsigned char* top = pixels.data() + static_cast<std::size_t>(y) * rowSize;
        unsigned char* bottom = pixels.data() + static_cast<std::size_t>(height - 1 - y) * rowSize;
        std::memcpy(temp.data(), top, rowSize);
        std::memcpy(top, bottom, rowSize);
        std::memcpy(bottom, temp.data(), rowSize);
    }
}

bool DecodePreviewBytes(
    const std::vector<unsigned char>& encodedBytes,
    std::vector<unsigned char>& outPixels,
    int& outW,
    int& outH) {
    int channels = 4;
    return !encodedBytes.empty() && DecodeImageBytes(encodedBytes, outPixels, outW, outH, channels);
}

bool ResolveProjectPreviewPixels(
    const StackFormat::ProjectDocument& project,
    const std::filesystem::path* fallbackAssetPath,
    const std::vector<unsigned char>* fallbackAssetBytes,
    std::vector<unsigned char>& outPixels,
    int& outW,
    int& outH,
    std::string& outStatus) {
    outPixels.clear();
    outW = 0;
    outH = 0;
    outStatus.clear();

    const std::string projectKind = project.metadata.projectKind.empty()
        ? StackFormat::kEditorProjectKind
        : project.metadata.projectKind;
    const bool isComposite = projectKind == StackFormat::kCompositeProjectKind;
    const bool isRender = projectKind == StackFormat::kRenderProjectKind;

    std::vector<unsigned char> sourcePngBytes = project.sourceImageBytes;
    std::vector<unsigned char> graphSourcePngBytes;
    if (projectKind == StackFormat::kEditorProjectKind &&
        ExtractEmbeddedGraphSourcePng(project.pipelineData, graphSourcePngBytes)) {
        sourcePngBytes = std::move(graphSourcePngBytes);
    }

    std::vector<unsigned char> sourcePixels;
    int sourceW = 0;
    int sourceH = 0;
    const bool hasSourcePixels = DecodePreviewBytes(sourcePngBytes, sourcePixels, sourceW, sourceH);

    if (isComposite || isRender) {
        if (hasSourcePixels) {
            outPixels = std::move(sourcePixels);
            outW = sourceW;
            outH = sourceH;
            outStatus = isComposite ? "Using embedded composite preview." : "Using embedded render preview.";
            return true;
        }
        if (DecodePreviewBytes(project.thumbnailBytes, outPixels, outW, outH)) {
            outStatus = "Using embedded thumbnail preview.";
            return true;
        }
        outStatus = "This project does not contain a usable embedded preview image.";
        return false;
    }

    if (fallbackAssetBytes != nullptr && !fallbackAssetBytes->empty()) {
        if (DecodePreviewBytes(*fallbackAssetBytes, outPixels, outW, outH)) {
            outStatus = "Using the imported saved rendered preview.";
            return true;
        }
    }

    if (fallbackAssetPath != nullptr && LoadRgbaImageFromFile(*fallbackAssetPath, outPixels, outW, outH)) {
        outStatus = "Using the saved rendered preview.";
        return true;
    }

    if (hasSourcePixels && !project.pipelineData.is_null()) {
        EditorModule previewEditor;
        previewEditor.Initialize();
        previewEditor.LoadSourceFromPixels(sourcePixels.data(), sourceW, sourceH, 4);
        previewEditor.DeserializePipeline(project.pipelineData);

        bool hasGraphRawRenderNode = false;
        for (const auto& node : previewEditor.GetNodeGraph().GetNodes()) {
            if (node.kind == EditorNodeGraph::NodeKind::RawNeuralDenoise ||
                node.kind == EditorNodeGraph::NodeKind::RawDecode ||
                node.kind == EditorNodeGraph::NodeKind::RawDevelop) {
                hasGraphRawRenderNode = true;
                break;
            }
        }

        if (hasGraphRawRenderNode) {
            previewEditor.GetPipeline().ExecuteGraph(previewEditor.BuildGraphSnapshot());
        } else {
            previewEditor.GetPipeline().Execute(previewEditor.GetLayers());
        }

        int renderedW = 0;
        int renderedH = 0;
        std::vector<unsigned char> renderedPixels = previewEditor.GetPipeline().GetOutputPixels(renderedW, renderedH);
        const bool renderedLooksBlank =
            !renderedPixels.empty() &&
            !HasMeaningfulPixels(renderedPixels) &&
            HasMeaningfulPixels(sourcePixels);
        if (!renderedPixels.empty() && renderedW > 0 && renderedH > 0 && !renderedLooksBlank) {
            FlipImageRowsInPlace(renderedPixels, renderedW, renderedH, 4);
            outPixels = std::move(renderedPixels);
            outW = renderedW;
            outH = renderedH;
            outStatus = "Rendered a live preview from the editor pipeline.";
            return true;
        }
    }

    if (DecodePreviewBytes(project.thumbnailBytes, outPixels, outW, outH)) {
        outStatus = "Using embedded thumbnail preview.";
        return true;
    }

    if (hasSourcePixels) {
        outPixels = std::move(sourcePixels);
        outW = sourceW;
        outH = sourceH;
        outStatus = "Using source image fallback.";
        return true;
    }

    outStatus = "Failed to build a preview from the project, its rendered asset, thumbnail, or source image.";
    return false;
}

} // namespace Stack::Library::ImageHelpers
