#include "EditorNodeGraphDefinitions.h"

#include "Editor/LayerRegistry.h"

namespace EditorNodeGraphDefinitions {
namespace {

const char* ScopeTitle(EditorNodeGraph::ScopeKind kind) {
    switch (kind) {
        case EditorNodeGraph::ScopeKind::Histogram: return "Histogram";
        case EditorNodeGraph::ScopeKind::Vectorscope: return "Vectorscope";
        case EditorNodeGraph::ScopeKind::RGBParade: return "RGB Parade";
    }
    return "Scope";
}

const char* MaskTitle(EditorNodeGraph::MaskGeneratorKind kind) {
    switch (kind) {
        case EditorNodeGraph::MaskGeneratorKind::Solid: return "Solid Mask";
        case EditorNodeGraph::MaskGeneratorKind::LinearGradient: return "Linear Gradient Mask";
        case EditorNodeGraph::MaskGeneratorKind::RadialGradient: return "Radial Gradient Mask";
        case EditorNodeGraph::MaskGeneratorKind::Noise: return "Noise Mask";
    }
    return "Mask";
}

const char* MaskCombineTitle(EditorNodeGraph::MaskCombineMode mode) {
    switch (mode) {
        case EditorNodeGraph::MaskCombineMode::Add: return "Add Mask";
        case EditorNodeGraph::MaskCombineMode::Subtract: return "Subtract Mask";
        case EditorNodeGraph::MaskCombineMode::Intersect: return "Intersect Mask";
        case EditorNodeGraph::MaskCombineMode::Exclude: return "Difference Mask";
    }
    return "Mask Combine";
}

const char* MaskUtilityTitle(EditorNodeGraph::MaskUtilityKind kind) {
    switch (kind) {
        case EditorNodeGraph::MaskUtilityKind::Invert: return "Invert Mask";
        case EditorNodeGraph::MaskUtilityKind::Levels: return "Remap Mask";
        case EditorNodeGraph::MaskUtilityKind::Threshold: return "Threshold Mask";
    }
    return "Mask Utility";
}

const char* ImageToMaskTitle(EditorNodeGraph::ImageToMaskKind kind) {
    switch (kind) {
        case EditorNodeGraph::ImageToMaskKind::Luminance: return "Luminance Mask";
        case EditorNodeGraph::ImageToMaskKind::SampledRange: return "Sampled Range Mask";
    }
    return "Image To Mask";
}

const char* ImageGeneratorTitle(EditorNodeGraph::ImageGeneratorKind kind) {
    switch (kind) {
        case EditorNodeGraph::ImageGeneratorKind::SolidColor: return "Solid Color Image";
        case EditorNodeGraph::ImageGeneratorKind::ColorGradient: return "Color Gradient Image";
        case EditorNodeGraph::ImageGeneratorKind::Square: return "Square";
        case EditorNodeGraph::ImageGeneratorKind::Circle: return "Circle";
        case EditorNodeGraph::ImageGeneratorKind::Text: return "Text";
    }
    return "Generated Image";
}

const char* DataMathTitle(EditorNodeGraph::DataMathMode mode) {
    switch (mode) {
        case EditorNodeGraph::DataMathMode::Clamp: return "Clamp";
        case EditorNodeGraph::DataMathMode::Add: return "Add";
        case EditorNodeGraph::DataMathMode::Subtract: return "Subtract";
        case EditorNodeGraph::DataMathMode::Multiply: return "Multiply";
        case EditorNodeGraph::DataMathMode::Divide: return "Divide";
        case EditorNodeGraph::DataMathMode::Average: return "Average";
        case EditorNodeGraph::DataMathMode::Min: return "Minimum";
        case EditorNodeGraph::DataMathMode::Max: return "Maximum";
        case EditorNodeGraph::DataMathMode::Difference: return "Difference";
        case EditorNodeGraph::DataMathMode::Remap: return "Remap";
    }
    return "Math";
}

std::string BuildPreviewKey(EditorNodeGraph::NodeKind kind, int value) {
    switch (kind) {
        case EditorNodeGraph::NodeKind::Output: return "output";
        case EditorNodeGraph::NodeKind::Preview: return "preview";
        case EditorNodeGraph::NodeKind::Scope:
            switch (static_cast<EditorNodeGraph::ScopeKind>(value)) {
                case EditorNodeGraph::ScopeKind::Histogram: return "scope:histogram";
                case EditorNodeGraph::ScopeKind::Vectorscope: return "scope:vectorscope";
                case EditorNodeGraph::ScopeKind::RGBParade: return "scope:rgbparade";
            }
            return "scope";
        case EditorNodeGraph::NodeKind::RawNeuralDenoise: return "raw-neural-denoise";
        case EditorNodeGraph::NodeKind::RawDecode: return "raw-decode";
        case EditorNodeGraph::NodeKind::RawDevelop: return "develop";
        case EditorNodeGraph::NodeKind::RawDetailAutoMask: return "raw-detail-automask";
        case EditorNodeGraph::NodeKind::RawDetailFusion: return "raw-detail-fusion";
        case EditorNodeGraph::NodeKind::HdrMerge: return "hdr-merge";
        case EditorNodeGraph::NodeKind::Lut: return "lut";
        case EditorNodeGraph::NodeKind::CustomMask: return "custom-mask";
        case EditorNodeGraph::NodeKind::Mix: return "blend-images";
        case EditorNodeGraph::NodeKind::ChannelSplit: return "channel-split";
        case EditorNodeGraph::NodeKind::ChannelCombine: return "channel-combine";
        case EditorNodeGraph::NodeKind::Composite: return "composite";
        case EditorNodeGraph::NodeKind::Layer: {
            const LayerDescriptor* descriptor = LayerRegistry::GetDescriptor(static_cast<LayerType>(value));
            return descriptor && descriptor->typeId ? std::string("layer:") + descriptor->typeId : "layer";
        }
        case EditorNodeGraph::NodeKind::MaskGenerator:
            switch (static_cast<EditorNodeGraph::MaskGeneratorKind>(value)) {
                case EditorNodeGraph::MaskGeneratorKind::Solid: return "mask:solid";
                case EditorNodeGraph::MaskGeneratorKind::LinearGradient: return "mask:linear-gradient";
                case EditorNodeGraph::MaskGeneratorKind::RadialGradient: return "mask:radial-gradient";
                case EditorNodeGraph::MaskGeneratorKind::Noise: return "mask:noise";
            }
            return "mask";
        case EditorNodeGraph::NodeKind::MaskCombine:
            switch (static_cast<EditorNodeGraph::MaskCombineMode>(value)) {
                case EditorNodeGraph::MaskCombineMode::Add: return "mask-combine:add";
                case EditorNodeGraph::MaskCombineMode::Subtract: return "mask-combine:subtract";
                case EditorNodeGraph::MaskCombineMode::Intersect: return "mask-combine:intersect";
                case EditorNodeGraph::MaskCombineMode::Exclude: return "mask-combine:exclude";
            }
            return "mask-combine";
        case EditorNodeGraph::NodeKind::MaskUtility:
            switch (static_cast<EditorNodeGraph::MaskUtilityKind>(value)) {
                case EditorNodeGraph::MaskUtilityKind::Invert: return "mask-utility:invert";
                case EditorNodeGraph::MaskUtilityKind::Levels: return "mask-utility:levels";
                case EditorNodeGraph::MaskUtilityKind::Threshold: return "mask-utility:threshold";
            }
            return "mask-utility";
        case EditorNodeGraph::NodeKind::ImageToMask:
            switch (static_cast<EditorNodeGraph::ImageToMaskKind>(value)) {
                case EditorNodeGraph::ImageToMaskKind::Luminance: return "image-to-mask:luminance";
                case EditorNodeGraph::ImageToMaskKind::SampledRange: return "image-to-mask:sampled-range";
            }
            return "image-to-mask";
        case EditorNodeGraph::NodeKind::ImageGenerator:
            switch (static_cast<EditorNodeGraph::ImageGeneratorKind>(value)) {
                case EditorNodeGraph::ImageGeneratorKind::SolidColor: return "image-generator:solid-color";
                case EditorNodeGraph::ImageGeneratorKind::ColorGradient: return "image-generator:color-gradient";
                case EditorNodeGraph::ImageGeneratorKind::Square: return "image-generator:square";
                case EditorNodeGraph::ImageGeneratorKind::Circle: return "image-generator:circle";
                case EditorNodeGraph::ImageGeneratorKind::Text: return "image-generator:text";
            }
            return "image-generator";
        case EditorNodeGraph::NodeKind::DataMath:
            switch (static_cast<EditorNodeGraph::DataMathMode>(value)) {
                case EditorNodeGraph::DataMathMode::Clamp: return "data-math:clamp";
                case EditorNodeGraph::DataMathMode::Add: return "data-math:add";
                case EditorNodeGraph::DataMathMode::Subtract: return "data-math:subtract";
                case EditorNodeGraph::DataMathMode::Multiply: return "data-math:multiply";
                case EditorNodeGraph::DataMathMode::Divide: return "data-math:divide";
                case EditorNodeGraph::DataMathMode::Average: return "data-math:average";
                case EditorNodeGraph::DataMathMode::Min: return "data-math:min";
                case EditorNodeGraph::DataMathMode::Max: return "data-math:max";
                case EditorNodeGraph::DataMathMode::Difference: return "data-math:difference";
                case EditorNodeGraph::DataMathMode::Remap: return "data-math:remap";
            }
            return "data-math";
        case EditorNodeGraph::NodeKind::Image:
        case EditorNodeGraph::NodeKind::RawSource:
            break;
    }
    return "catalog-entry";
}

NodeCatalogEntry MakeCatalogEntry(
    EditorNodeGraph::NodeKind kind,
    int value,
    std::string label,
    std::string category,
    NodeCatalogPreviewStrategy strategy = NodeCatalogPreviewStrategy::Auto,
    std::uint32_t previewRecipeVersion = 1) {
    NodeCatalogEntry entry;
    entry.kind = kind;
    entry.value = value;
    entry.label = std::move(label);
    entry.category = std::move(category);
    entry.previewKey = BuildPreviewKey(kind, value);
    entry.previewRecipeVersion = previewRecipeVersion;
    entry.previewStrategy = strategy;
    return entry;
}

} // namespace

void ApplyNodeMetadata(EditorNodeGraph::Node& node) {
    switch (node.kind) {
        case EditorNodeGraph::NodeKind::Image:
            node.title = node.image.label.empty() ? "Image" : node.image.label;
            break;
        case EditorNodeGraph::NodeKind::RawSource:
            node.title = node.rawSource.label.empty() ? "RAW" : node.rawSource.label;
            break;
        case EditorNodeGraph::NodeKind::RawNeuralDenoise:
            node.title = "RAW/CFA Neural Denoise";
            break;
        case EditorNodeGraph::NodeKind::RawDecode:
            node.title = "RAW Decode";
            break;
        case EditorNodeGraph::NodeKind::RawDevelop:
            node.title = "Develop";
            break;
        case EditorNodeGraph::NodeKind::RawDetailAutoMask:
            node.title = "RAW Detail Auto Mask";
            break;
        case EditorNodeGraph::NodeKind::RawDetailFusion:
            node.title = "Pre-Local Exposure";
            break;
        case EditorNodeGraph::NodeKind::HdrMerge:
            node.title = "HDR Merge";
            break;
        case EditorNodeGraph::NodeKind::Lut:
            node.title = "LUT";
            break;
        case EditorNodeGraph::NodeKind::Layer: {
            const LayerDescriptor* descriptor = LayerRegistry::GetDescriptor(node.layerType);
            node.typeId = descriptor ? descriptor->typeId : "";
            node.title = descriptor ? descriptor->displayName : "Layer";
            break;
        }
        case EditorNodeGraph::NodeKind::Output:
            node.title = node.outputEnabled ? "Output" : "Deactivated";
            break;
        case EditorNodeGraph::NodeKind::Composite:
            node.title = "Composite";
            break;
        case EditorNodeGraph::NodeKind::Scope:
            node.title = ScopeTitle(node.scopeKind);
            node.expanded = true;
            break;
        case EditorNodeGraph::NodeKind::Preview:
            node.title = "Preview";
            node.expanded = true;
            break;
        case EditorNodeGraph::NodeKind::MaskGenerator:
            node.title = MaskTitle(node.maskKind);
            break;
        case EditorNodeGraph::NodeKind::CustomMask:
            node.title = "Custom Mask";
            node.expanded = true;
            break;
        case EditorNodeGraph::NodeKind::MaskCombine:
            node.title = MaskCombineTitle(node.maskCombineMode);
            break;
        case EditorNodeGraph::NodeKind::MaskUtility:
            node.title = MaskUtilityTitle(node.maskUtilityKind);
            break;
        case EditorNodeGraph::NodeKind::ImageToMask:
            node.title = ImageToMaskTitle(node.imageToMaskKind);
            break;
        case EditorNodeGraph::NodeKind::ImageGenerator:
            node.title = ImageGeneratorTitle(node.imageGeneratorKind);
            break;
        case EditorNodeGraph::NodeKind::Mix:
            node.title = "Blend Images";
            break;
        case EditorNodeGraph::NodeKind::ChannelSplit:
            node.title = "Channel Split";
            break;
        case EditorNodeGraph::NodeKind::ChannelCombine:
            node.title = "Channel Combine";
            break;
        case EditorNodeGraph::NodeKind::DataMath:
            node.title = DataMathTitle(node.dataMathMode);
            break;
    }
}

std::vector<EditorNodeGraph::SocketDefinition> BuildSockets(const EditorNodeGraph::Node& node, bool visibleOnly) {
    std::vector<EditorNodeGraph::SocketDefinition> sockets;
    auto add = [&](const char* id,
                   EditorNodeGraph::SocketDirection direction,
                   EditorNodeGraph::SocketType type,
                   const char* label,
                   bool optional,
                   bool visible) {
        if (visibleOnly && !visible) {
            return;
        }
        sockets.push_back(EditorNodeGraph::SocketDefinition{ id, node.id, direction, type, label, optional, visible });
    };

    switch (node.kind) {
        case EditorNodeGraph::NodeKind::Image:
            add(EditorNodeGraph::kImageOutputSocketId, EditorNodeGraph::SocketDirection::Output, EditorNodeGraph::SocketType::Image, "Image", false, true);
            break;
        case EditorNodeGraph::NodeKind::RawSource:
            add(EditorNodeGraph::kRawOutputSocketId, EditorNodeGraph::SocketDirection::Output, EditorNodeGraph::SocketType::Raw, "RAW", false, true);
            break;
        case EditorNodeGraph::NodeKind::RawNeuralDenoise:
            add(EditorNodeGraph::kRawInputSocketId, EditorNodeGraph::SocketDirection::Input, EditorNodeGraph::SocketType::Raw, "RAW", false, true);
            add(EditorNodeGraph::kRawOutputSocketId, EditorNodeGraph::SocketDirection::Output, EditorNodeGraph::SocketType::Raw, "RAW", false, true);
            break;
        case EditorNodeGraph::NodeKind::RawDecode:
            add(EditorNodeGraph::kRawInputSocketId, EditorNodeGraph::SocketDirection::Input, EditorNodeGraph::SocketType::Raw, "RAW", false, true);
            add(EditorNodeGraph::kImageOutputSocketId, EditorNodeGraph::SocketDirection::Output, EditorNodeGraph::SocketType::Image, "Image", false, true);
            break;
        case EditorNodeGraph::NodeKind::RawDevelop:
            add(EditorNodeGraph::kRawInputSocketId, EditorNodeGraph::SocketDirection::Input, EditorNodeGraph::SocketType::Raw, "RAW", false, true);
            add(EditorNodeGraph::kMaskInputSocketId, EditorNodeGraph::SocketDirection::Input, EditorNodeGraph::SocketType::Mask, "Finish Mask", true, true);
            add(EditorNodeGraph::kImageOutputSocketId, EditorNodeGraph::SocketDirection::Output, EditorNodeGraph::SocketType::Image, "Image", false, true);
            add(EditorNodeGraph::kPreFinishImageOutputSocketId, EditorNodeGraph::SocketDirection::Output, EditorNodeGraph::SocketType::Image, "Pre-Finish", false, false);
            break;
        case EditorNodeGraph::NodeKind::RawDetailAutoMask:
            add(EditorNodeGraph::kImageInputSocketId, EditorNodeGraph::SocketDirection::Input, EditorNodeGraph::SocketType::Image, "Image", false, true);
            add(EditorNodeGraph::kMaskOutputSocketId, EditorNodeGraph::SocketDirection::Output, EditorNodeGraph::SocketType::Mask, "EV Map", false, true);
            break;
        case EditorNodeGraph::NodeKind::RawDetailFusion:
            add(EditorNodeGraph::kImageInputSocketId, EditorNodeGraph::SocketDirection::Input, EditorNodeGraph::SocketType::Image, "Image", false, true);
            add(EditorNodeGraph::kMaskInputSocketId, EditorNodeGraph::SocketDirection::Input, EditorNodeGraph::SocketType::Mask, "Hybrid Mask", true, true);
            add(EditorNodeGraph::kImageOutputSocketId, EditorNodeGraph::SocketDirection::Output, EditorNodeGraph::SocketType::Image, "Image", false, true);
            add(EditorNodeGraph::kMaskOutputSocketId, EditorNodeGraph::SocketDirection::Output, EditorNodeGraph::SocketType::Mask, "Gain Mask", false, true);
            break;
        case EditorNodeGraph::NodeKind::HdrMerge:
            add(EditorNodeGraph::kHdrMergeInput1SocketId, EditorNodeGraph::SocketDirection::Input, EditorNodeGraph::SocketType::Image, "Image 1", false, true);
            add(EditorNodeGraph::kHdrMergeInput2SocketId, EditorNodeGraph::SocketDirection::Input, EditorNodeGraph::SocketType::Image, "Image 2", true, true);
            add(EditorNodeGraph::kHdrMergeInput3SocketId, EditorNodeGraph::SocketDirection::Input, EditorNodeGraph::SocketType::Image, "Image 3", true, true);
            add(EditorNodeGraph::kImageOutputSocketId, EditorNodeGraph::SocketDirection::Output, EditorNodeGraph::SocketType::Image, "Scene HDR", false, true);
            break;
        case EditorNodeGraph::NodeKind::Lut:
            add(EditorNodeGraph::kImageInputSocketId, EditorNodeGraph::SocketDirection::Input, EditorNodeGraph::SocketType::Image, "Image", false, true);
            add(EditorNodeGraph::kMaskInputSocketId, EditorNodeGraph::SocketDirection::Input, EditorNodeGraph::SocketType::Mask, "Mask", true, true);
            add(EditorNodeGraph::kImageOutputSocketId, EditorNodeGraph::SocketDirection::Output, EditorNodeGraph::SocketType::Image, "Image", false, true);
            break;
        case EditorNodeGraph::NodeKind::Layer:
            add(EditorNodeGraph::kImageInputSocketId, EditorNodeGraph::SocketDirection::Input, EditorNodeGraph::SocketType::Image, "Image", false, true);
            add(EditorNodeGraph::kMaskInputSocketId, EditorNodeGraph::SocketDirection::Input, EditorNodeGraph::SocketType::Mask, "Mask", true, true);
            add(EditorNodeGraph::kImageOutputSocketId, EditorNodeGraph::SocketDirection::Output, EditorNodeGraph::SocketType::Image, "Image", false, true);
            break;
        case EditorNodeGraph::NodeKind::Output:
            add(EditorNodeGraph::kImageInputSocketId, EditorNodeGraph::SocketDirection::Input, EditorNodeGraph::SocketType::Image, "Image", false, true);
            break;
        case EditorNodeGraph::NodeKind::Composite:
            break;
        case EditorNodeGraph::NodeKind::Scope:
            add(EditorNodeGraph::kScopeInputSocketId, EditorNodeGraph::SocketDirection::Input, EditorNodeGraph::SocketType::Analysis, "Scope", false, true);
            break;
        case EditorNodeGraph::NodeKind::Preview:
            add(EditorNodeGraph::kPreviewInputSocketId, EditorNodeGraph::SocketDirection::Input, EditorNodeGraph::SocketType::Analysis, "Image / Mask", false, true);
            break;
        case EditorNodeGraph::NodeKind::MaskGenerator:
            add(EditorNodeGraph::kMaskOutputSocketId, EditorNodeGraph::SocketDirection::Output, EditorNodeGraph::SocketType::Mask, "Mask", false, true);
            break;
        case EditorNodeGraph::NodeKind::CustomMask:
            add(EditorNodeGraph::kMaskOutputSocketId, EditorNodeGraph::SocketDirection::Output, EditorNodeGraph::SocketType::Mask, "Mask", false, true);
            break;
        case EditorNodeGraph::NodeKind::MaskCombine:
            add(EditorNodeGraph::kMaskCombineInputASocketId, EditorNodeGraph::SocketDirection::Input, EditorNodeGraph::SocketType::Mask, "Mask A", false, true);
            add(EditorNodeGraph::kMaskCombineInputBSocketId, EditorNodeGraph::SocketDirection::Input, EditorNodeGraph::SocketType::Mask, "Mask B", false, true);
            add(EditorNodeGraph::kMaskOutputSocketId, EditorNodeGraph::SocketDirection::Output, EditorNodeGraph::SocketType::Mask, "Mask Out", false, true);
            break;
        case EditorNodeGraph::NodeKind::MaskUtility:
            add(EditorNodeGraph::kMaskInputSocketId, EditorNodeGraph::SocketDirection::Input, EditorNodeGraph::SocketType::Mask, "Mask", false, true);
            add(EditorNodeGraph::kMaskOutputSocketId, EditorNodeGraph::SocketDirection::Output, EditorNodeGraph::SocketType::Mask, "Mask Out", false, true);
            break;
        case EditorNodeGraph::NodeKind::ImageToMask:
            add(EditorNodeGraph::kImageInputSocketId, EditorNodeGraph::SocketDirection::Input, EditorNodeGraph::SocketType::Image, "Image", false, true);
            add(EditorNodeGraph::kMaskOutputSocketId, EditorNodeGraph::SocketDirection::Output, EditorNodeGraph::SocketType::Mask, "Mask Out", false, true);
            break;
        case EditorNodeGraph::NodeKind::ImageGenerator:
            add(EditorNodeGraph::kImageOutputSocketId, EditorNodeGraph::SocketDirection::Output, EditorNodeGraph::SocketType::Image, "Image", false, true);
            break;
        case EditorNodeGraph::NodeKind::Mix:
            add(EditorNodeGraph::kMixInputASocketId, EditorNodeGraph::SocketDirection::Input, EditorNodeGraph::SocketType::Image, "A", false, true);
            add(EditorNodeGraph::kMixInputBSocketId, EditorNodeGraph::SocketDirection::Input, EditorNodeGraph::SocketType::Image, "B", false, true);
            add(EditorNodeGraph::kMixFactorSocketId, EditorNodeGraph::SocketDirection::Input, EditorNodeGraph::SocketType::Mask, "Factor", true, true);
            add(EditorNodeGraph::kImageOutputSocketId, EditorNodeGraph::SocketDirection::Output, EditorNodeGraph::SocketType::Image, "Image", false, true);
            break;
        case EditorNodeGraph::NodeKind::DataMath:
            add(EditorNodeGraph::kMixInputASocketId, EditorNodeGraph::SocketDirection::Input, EditorNodeGraph::SocketType::Image, "Data A", false, true);
            add(EditorNodeGraph::kMixInputBSocketId, EditorNodeGraph::SocketDirection::Input, EditorNodeGraph::SocketType::Image, "Data B", true, true);
            add(EditorNodeGraph::kImageOutputSocketId, EditorNodeGraph::SocketDirection::Output, EditorNodeGraph::SocketType::Image, "Data Out", false, true);
            break;
        case EditorNodeGraph::NodeKind::ChannelSplit:
            add(EditorNodeGraph::kImageInputSocketId, EditorNodeGraph::SocketDirection::Input, EditorNodeGraph::SocketType::Image, "Image", false, true);
            add("r", EditorNodeGraph::SocketDirection::Output, EditorNodeGraph::SocketType::Mask, "R", false, true);
            add("g", EditorNodeGraph::SocketDirection::Output, EditorNodeGraph::SocketType::Mask, "G", false, true);
            add("b", EditorNodeGraph::SocketDirection::Output, EditorNodeGraph::SocketType::Mask, "B", false, true);
            add("a", EditorNodeGraph::SocketDirection::Output, EditorNodeGraph::SocketType::Mask, "A", false, true);
            break;
        case EditorNodeGraph::NodeKind::ChannelCombine:
            add("r", EditorNodeGraph::SocketDirection::Input, EditorNodeGraph::SocketType::Mask, "R", true, true);
            add("g", EditorNodeGraph::SocketDirection::Input, EditorNodeGraph::SocketType::Mask, "G", true, true);
            add("b", EditorNodeGraph::SocketDirection::Input, EditorNodeGraph::SocketType::Mask, "B", true, true);
            add("a", EditorNodeGraph::SocketDirection::Input, EditorNodeGraph::SocketType::Mask, "A", true, true);
            add(EditorNodeGraph::kImageOutputSocketId, EditorNodeGraph::SocketDirection::Output, EditorNodeGraph::SocketType::Image, "Image", false, true);
            break;
    }

    return sockets;
}

std::string DefaultInputSocket(const EditorNodeGraph::Node& node) {
    switch (node.kind) {
        case EditorNodeGraph::NodeKind::Layer:
        case EditorNodeGraph::NodeKind::Lut:
        case EditorNodeGraph::NodeKind::RawDetailAutoMask:
        case EditorNodeGraph::NodeKind::RawDetailFusion:
        case EditorNodeGraph::NodeKind::Output:
        case EditorNodeGraph::NodeKind::ChannelSplit:
            return EditorNodeGraph::kImageInputSocketId;
        case EditorNodeGraph::NodeKind::HdrMerge:
            return EditorNodeGraph::kHdrMergeInput1SocketId;
        case EditorNodeGraph::NodeKind::RawDecode:
        case EditorNodeGraph::NodeKind::RawDevelop:
        case EditorNodeGraph::NodeKind::RawNeuralDenoise:
            return EditorNodeGraph::kRawInputSocketId;
        case EditorNodeGraph::NodeKind::ChannelCombine:
            return "r";
        case EditorNodeGraph::NodeKind::Composite:
            break;
        case EditorNodeGraph::NodeKind::Mix:
        case EditorNodeGraph::NodeKind::DataMath:
            return EditorNodeGraph::kMixInputASocketId;
        case EditorNodeGraph::NodeKind::Scope:
            return EditorNodeGraph::kScopeInputSocketId;
        case EditorNodeGraph::NodeKind::Preview:
            return EditorNodeGraph::kPreviewInputSocketId;
        case EditorNodeGraph::NodeKind::MaskUtility:
            return EditorNodeGraph::kMaskInputSocketId;
        case EditorNodeGraph::NodeKind::MaskCombine:
            return EditorNodeGraph::kMaskCombineInputASocketId;
        case EditorNodeGraph::NodeKind::ImageToMask:
            return EditorNodeGraph::kImageInputSocketId;
        case EditorNodeGraph::NodeKind::MaskGenerator:
        case EditorNodeGraph::NodeKind::CustomMask:
        case EditorNodeGraph::NodeKind::ImageGenerator:
        case EditorNodeGraph::NodeKind::RawSource:
        case EditorNodeGraph::NodeKind::Image:
            break;
    }
    return {};
}

std::string DefaultOutputSocket(const EditorNodeGraph::Node& node) {
    switch (node.kind) {
        case EditorNodeGraph::NodeKind::Image:
        case EditorNodeGraph::NodeKind::RawDecode:
        case EditorNodeGraph::NodeKind::RawDevelop:
        case EditorNodeGraph::NodeKind::RawDetailFusion:
        case EditorNodeGraph::NodeKind::HdrMerge:
        case EditorNodeGraph::NodeKind::Lut:
        case EditorNodeGraph::NodeKind::Layer:
        case EditorNodeGraph::NodeKind::Mix:
        case EditorNodeGraph::NodeKind::ImageGenerator:
        case EditorNodeGraph::NodeKind::ChannelCombine:
        case EditorNodeGraph::NodeKind::DataMath:
            return EditorNodeGraph::kImageOutputSocketId;
        case EditorNodeGraph::NodeKind::RawSource:
        case EditorNodeGraph::NodeKind::RawNeuralDenoise:
            return EditorNodeGraph::kRawOutputSocketId;
        case EditorNodeGraph::NodeKind::ChannelSplit:
            return "r";
        case EditorNodeGraph::NodeKind::MaskGenerator:
        case EditorNodeGraph::NodeKind::CustomMask:
        case EditorNodeGraph::NodeKind::MaskCombine:
        case EditorNodeGraph::NodeKind::MaskUtility:
        case EditorNodeGraph::NodeKind::ImageToMask:
        case EditorNodeGraph::NodeKind::RawDetailAutoMask:
            return EditorNodeGraph::kMaskOutputSocketId;
        case EditorNodeGraph::NodeKind::Composite:
        case EditorNodeGraph::NodeKind::Output:
        case EditorNodeGraph::NodeKind::Scope:
        case EditorNodeGraph::NodeKind::Preview:
            break;
    }
    return {};
}

std::vector<NodeCatalogEntry> BuildNodeCatalogEntries() {
    std::vector<NodeCatalogEntry> entries;
    entries.push_back(MakeCatalogEntry(
        EditorNodeGraph::NodeKind::Output,
        0,
        "Output",
        "Input / Output",
        NodeCatalogPreviewStrategy::FallbackOnly));
    for (const LayerDescriptor& descriptor : LayerRegistry::GetAllDescriptors()) {
        if (!LayerRegistry::ShouldShowInNodeBrowser(descriptor)) {
            continue;
        }
        std::string label = descriptor.displayName ? descriptor.displayName : "Layer";
        if (descriptor.lifecycleStatus == LayerLifecycleStatus::Experimental) {
            label += " (Experimental)";
        } else if (descriptor.lifecycleStatus == LayerLifecycleStatus::NeedsFix) {
            label += " (Needs Fix)";
        }
        entries.push_back(MakeCatalogEntry(
            EditorNodeGraph::NodeKind::Layer,
            static_cast<int>(descriptor.type),
            std::move(label),
            descriptor.categoryName ? descriptor.categoryName : "Layers"));
    }
    entries.push_back(MakeCatalogEntry(EditorNodeGraph::NodeKind::Scope, static_cast<int>(EditorNodeGraph::ScopeKind::Histogram), "Histogram", "Input / Output", NodeCatalogPreviewStrategy::FallbackOnly));
    entries.push_back(MakeCatalogEntry(EditorNodeGraph::NodeKind::Scope, static_cast<int>(EditorNodeGraph::ScopeKind::Vectorscope), "Vectorscope", "Input / Output", NodeCatalogPreviewStrategy::FallbackOnly));
    entries.push_back(MakeCatalogEntry(EditorNodeGraph::NodeKind::Scope, static_cast<int>(EditorNodeGraph::ScopeKind::RGBParade), "RGB Parade", "Input / Output", NodeCatalogPreviewStrategy::FallbackOnly));
    entries.push_back(MakeCatalogEntry(EditorNodeGraph::NodeKind::Preview, 0, "Preview", "Input / Output", NodeCatalogPreviewStrategy::FallbackOnly));
    entries.push_back(MakeCatalogEntry(
        EditorNodeGraph::NodeKind::RawNeuralDenoise,
        0,
        "RAW/CFA Neural Denoise",
        "Input / Output",
        NodeCatalogPreviewStrategy::NoPreview,
        2));
    entries.push_back(MakeCatalogEntry(
        EditorNodeGraph::NodeKind::RawDecode,
        0,
        "RAW Decode",
        "Input / Output",
        NodeCatalogPreviewStrategy::NoPreview,
        2));
    entries.push_back(MakeCatalogEntry(
        EditorNodeGraph::NodeKind::RawDevelop,
        0,
        "Develop",
        "Input / Output",
        NodeCatalogPreviewStrategy::NoPreview,
        2));
    entries.push_back(MakeCatalogEntry(EditorNodeGraph::NodeKind::HdrMerge, 0, "HDR Merge", "Input / Output", NodeCatalogPreviewStrategy::FallbackOnly));
    entries.push_back(MakeCatalogEntry(EditorNodeGraph::NodeKind::CustomMask, 0, "Custom Mask", "Masks", NodeCatalogPreviewStrategy::FallbackOnly));
    entries.push_back(MakeCatalogEntry(EditorNodeGraph::NodeKind::MaskGenerator, static_cast<int>(EditorNodeGraph::MaskGeneratorKind::Solid), "Solid Mask", "Masks"));
    entries.push_back(MakeCatalogEntry(EditorNodeGraph::NodeKind::MaskGenerator, static_cast<int>(EditorNodeGraph::MaskGeneratorKind::LinearGradient), "Linear Gradient Mask", "Masks"));
    entries.push_back(MakeCatalogEntry(EditorNodeGraph::NodeKind::MaskGenerator, static_cast<int>(EditorNodeGraph::MaskGeneratorKind::RadialGradient), "Radial Gradient Mask", "Masks"));
    entries.push_back(MakeCatalogEntry(EditorNodeGraph::NodeKind::MaskGenerator, static_cast<int>(EditorNodeGraph::MaskGeneratorKind::Noise), "Noise Mask", "Masks"));
    entries.push_back(MakeCatalogEntry(EditorNodeGraph::NodeKind::MaskCombine, static_cast<int>(EditorNodeGraph::MaskCombineMode::Add), "Add Mask", "Mask / Math"));
    entries.push_back(MakeCatalogEntry(EditorNodeGraph::NodeKind::MaskCombine, static_cast<int>(EditorNodeGraph::MaskCombineMode::Subtract), "Subtract Mask", "Mask / Math"));
    entries.push_back(MakeCatalogEntry(EditorNodeGraph::NodeKind::MaskCombine, static_cast<int>(EditorNodeGraph::MaskCombineMode::Intersect), "Intersect Mask", "Mask / Math"));
    entries.push_back(MakeCatalogEntry(EditorNodeGraph::NodeKind::MaskCombine, static_cast<int>(EditorNodeGraph::MaskCombineMode::Exclude), "Difference Mask", "Mask / Math"));
    entries.push_back(MakeCatalogEntry(EditorNodeGraph::NodeKind::MaskUtility, static_cast<int>(EditorNodeGraph::MaskUtilityKind::Invert), "Invert Mask", "Mask / Math"));
    entries.push_back(MakeCatalogEntry(EditorNodeGraph::NodeKind::MaskUtility, static_cast<int>(EditorNodeGraph::MaskUtilityKind::Levels), "Remap Mask", "Mask / Math"));
    entries.push_back(MakeCatalogEntry(EditorNodeGraph::NodeKind::MaskUtility, static_cast<int>(EditorNodeGraph::MaskUtilityKind::Threshold), "Threshold Mask", "Mask / Math"));
    entries.push_back(MakeCatalogEntry(EditorNodeGraph::NodeKind::ImageToMask, static_cast<int>(EditorNodeGraph::ImageToMaskKind::Luminance), "Luminance Mask", "Mask / Math"));
    entries.push_back(MakeCatalogEntry(EditorNodeGraph::NodeKind::ImageToMask, static_cast<int>(EditorNodeGraph::ImageToMaskKind::SampledRange), "Sampled Range Mask", "Mask / Math"));
    entries.push_back(MakeCatalogEntry(EditorNodeGraph::NodeKind::DataMath, static_cast<int>(EditorNodeGraph::DataMathMode::Clamp), "Clamp", "Mask / Math"));
    entries.push_back(MakeCatalogEntry(EditorNodeGraph::NodeKind::DataMath, static_cast<int>(EditorNodeGraph::DataMathMode::Add), "Add", "Mask / Math"));
    entries.push_back(MakeCatalogEntry(EditorNodeGraph::NodeKind::DataMath, static_cast<int>(EditorNodeGraph::DataMathMode::Subtract), "Subtract", "Mask / Math"));
    entries.push_back(MakeCatalogEntry(EditorNodeGraph::NodeKind::DataMath, static_cast<int>(EditorNodeGraph::DataMathMode::Multiply), "Multiply", "Mask / Math"));
    entries.push_back(MakeCatalogEntry(EditorNodeGraph::NodeKind::DataMath, static_cast<int>(EditorNodeGraph::DataMathMode::Divide), "Divide", "Mask / Math"));
    entries.push_back(MakeCatalogEntry(EditorNodeGraph::NodeKind::DataMath, static_cast<int>(EditorNodeGraph::DataMathMode::Average), "Average", "Mask / Math"));
    entries.push_back(MakeCatalogEntry(EditorNodeGraph::NodeKind::DataMath, static_cast<int>(EditorNodeGraph::DataMathMode::Min), "Minimum", "Mask / Math"));
    entries.push_back(MakeCatalogEntry(EditorNodeGraph::NodeKind::DataMath, static_cast<int>(EditorNodeGraph::DataMathMode::Max), "Maximum", "Mask / Math"));
    entries.push_back(MakeCatalogEntry(EditorNodeGraph::NodeKind::DataMath, static_cast<int>(EditorNodeGraph::DataMathMode::Difference), "Difference", "Mask / Math"));
    entries.push_back(MakeCatalogEntry(EditorNodeGraph::NodeKind::DataMath, static_cast<int>(EditorNodeGraph::DataMathMode::Remap), "Remap", "Mask / Math"));
    entries.push_back(MakeCatalogEntry(EditorNodeGraph::NodeKind::ImageGenerator, static_cast<int>(EditorNodeGraph::ImageGeneratorKind::SolidColor), "Solid Color Image", "Texture / Generate"));
    entries.push_back(MakeCatalogEntry(EditorNodeGraph::NodeKind::ImageGenerator, static_cast<int>(EditorNodeGraph::ImageGeneratorKind::ColorGradient), "Color Gradient Image", "Texture / Generate"));
    entries.push_back(MakeCatalogEntry(EditorNodeGraph::NodeKind::ImageGenerator, static_cast<int>(EditorNodeGraph::ImageGeneratorKind::Square), "Square", "Texture / Generate"));
    entries.push_back(MakeCatalogEntry(EditorNodeGraph::NodeKind::ImageGenerator, static_cast<int>(EditorNodeGraph::ImageGeneratorKind::Circle), "Circle", "Texture / Generate"));
    entries.push_back(MakeCatalogEntry(EditorNodeGraph::NodeKind::ImageGenerator, static_cast<int>(EditorNodeGraph::ImageGeneratorKind::Text), "Text", "Texture / Generate"));
    entries.push_back(MakeCatalogEntry(EditorNodeGraph::NodeKind::Mix, 0, "Blend Images", "Image Operations"));
    entries.push_back(MakeCatalogEntry(EditorNodeGraph::NodeKind::Lut, 0, "LUT", "Image Operations"));
    entries.push_back(MakeCatalogEntry(EditorNodeGraph::NodeKind::ChannelSplit, 0, "Channel Split", "Channels"));
    entries.push_back(MakeCatalogEntry(EditorNodeGraph::NodeKind::ChannelCombine, 0, "Channel Combine", "Channels"));
    return entries;
}

EditorNodeGraph::Node BuildPrototypeNode(const NodeCatalogEntry& entry) {
    EditorNodeGraph::Node node;
    node.kind = entry.kind;
    switch (entry.kind) {
        case EditorNodeGraph::NodeKind::Layer:
            node.layerType = static_cast<LayerType>(entry.value);
            break;
        case EditorNodeGraph::NodeKind::Scope:
            node.scopeKind = static_cast<EditorNodeGraph::ScopeKind>(entry.value);
            break;
        case EditorNodeGraph::NodeKind::MaskGenerator:
            node.maskKind = static_cast<EditorNodeGraph::MaskGeneratorKind>(entry.value);
            break;
        case EditorNodeGraph::NodeKind::MaskCombine:
            node.maskCombineMode = static_cast<EditorNodeGraph::MaskCombineMode>(entry.value);
            break;
        case EditorNodeGraph::NodeKind::MaskUtility:
            node.maskUtilityKind = static_cast<EditorNodeGraph::MaskUtilityKind>(entry.value);
            break;
        case EditorNodeGraph::NodeKind::ImageToMask:
            node.imageToMaskKind = static_cast<EditorNodeGraph::ImageToMaskKind>(entry.value);
            break;
        case EditorNodeGraph::NodeKind::ImageGenerator:
            node.imageGeneratorKind = static_cast<EditorNodeGraph::ImageGeneratorKind>(entry.value);
            break;
        case EditorNodeGraph::NodeKind::DataMath:
            node.dataMathMode = static_cast<EditorNodeGraph::DataMathMode>(entry.value);
            break;
        default:
            break;
    }
    ApplyNodeMetadata(node);
    return node;
}

} // namespace EditorNodeGraphDefinitions
