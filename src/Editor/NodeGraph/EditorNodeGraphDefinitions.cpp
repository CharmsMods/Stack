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

const char* MaskUtilityTitle(EditorNodeGraph::MaskUtilityKind kind) {
    switch (kind) {
        case EditorNodeGraph::MaskUtilityKind::Invert: return "Invert Mask";
        case EditorNodeGraph::MaskUtilityKind::Levels: return "Levels Mask";
        case EditorNodeGraph::MaskUtilityKind::Threshold: return "Threshold Mask";
    }
    return "Mask Utility";
}

const char* ImageToMaskTitle(EditorNodeGraph::ImageToMaskKind kind) {
    switch (kind) {
        case EditorNodeGraph::ImageToMaskKind::Luminance: return "Luminance Mask";
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

} // namespace

void ApplyNodeMetadata(EditorNodeGraph::Node& node) {
    switch (node.kind) {
        case EditorNodeGraph::NodeKind::Image:
            node.title = node.image.label.empty() ? "Image" : node.image.label;
            break;
        case EditorNodeGraph::NodeKind::Layer: {
            const LayerDescriptor* descriptor = LayerRegistry::GetDescriptor(node.layerType);
            node.typeId = descriptor ? descriptor->typeId : "";
            node.title = descriptor ? descriptor->displayName : "Layer";
            break;
        }
        case EditorNodeGraph::NodeKind::Output:
            node.title = "Output";
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
            node.title = "Mix";
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
        case EditorNodeGraph::NodeKind::MaskUtility:
            add(EditorNodeGraph::kMaskInputSocketId, EditorNodeGraph::SocketDirection::Input, EditorNodeGraph::SocketType::Mask, "Mask", false, true);
            add(EditorNodeGraph::kMaskOutputSocketId, EditorNodeGraph::SocketDirection::Output, EditorNodeGraph::SocketType::Mask, "Mask", false, true);
            break;
        case EditorNodeGraph::NodeKind::ImageToMask:
            add(EditorNodeGraph::kImageInputSocketId, EditorNodeGraph::SocketDirection::Input, EditorNodeGraph::SocketType::Image, "Image", false, true);
            add(EditorNodeGraph::kMaskOutputSocketId, EditorNodeGraph::SocketDirection::Output, EditorNodeGraph::SocketType::Mask, "Mask", false, true);
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
    }

    return sockets;
}

std::string DefaultInputSocket(const EditorNodeGraph::Node& node) {
    switch (node.kind) {
        case EditorNodeGraph::NodeKind::Layer:
        case EditorNodeGraph::NodeKind::Output:
            return EditorNodeGraph::kImageInputSocketId;
        case EditorNodeGraph::NodeKind::Composite:
            break;
        case EditorNodeGraph::NodeKind::Mix:
            return EditorNodeGraph::kMixInputASocketId;
        case EditorNodeGraph::NodeKind::Scope:
            return EditorNodeGraph::kScopeInputSocketId;
        case EditorNodeGraph::NodeKind::Preview:
            return EditorNodeGraph::kPreviewInputSocketId;
        case EditorNodeGraph::NodeKind::MaskUtility:
            return EditorNodeGraph::kMaskInputSocketId;
        case EditorNodeGraph::NodeKind::ImageToMask:
            return EditorNodeGraph::kImageInputSocketId;
        case EditorNodeGraph::NodeKind::MaskGenerator:
        case EditorNodeGraph::NodeKind::ImageGenerator:
        case EditorNodeGraph::NodeKind::Image:
            break;
    }
    return {};
}

std::string DefaultOutputSocket(const EditorNodeGraph::Node& node) {
    switch (node.kind) {
        case EditorNodeGraph::NodeKind::Image:
        case EditorNodeGraph::NodeKind::Layer:
        case EditorNodeGraph::NodeKind::Mix:
        case EditorNodeGraph::NodeKind::ImageGenerator:
            return EditorNodeGraph::kImageOutputSocketId;
        case EditorNodeGraph::NodeKind::MaskGenerator:
        case EditorNodeGraph::NodeKind::MaskUtility:
        case EditorNodeGraph::NodeKind::ImageToMask:
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
    entries.push_back({ EditorNodeGraph::NodeKind::Output, 0, "Output", "Input / Output" });
    for (const LayerDescriptor& descriptor : LayerRegistry::GetAllDescriptors()) {
        entries.push_back({
            EditorNodeGraph::NodeKind::Layer,
            static_cast<int>(descriptor.type),
            descriptor.displayName ? descriptor.displayName : "Layer",
            descriptor.categoryName ? descriptor.categoryName : "Layers"
        });
    }
    entries.push_back({ EditorNodeGraph::NodeKind::Scope, static_cast<int>(EditorNodeGraph::ScopeKind::Histogram), "Histogram", "Input / Output" });
    entries.push_back({ EditorNodeGraph::NodeKind::Scope, static_cast<int>(EditorNodeGraph::ScopeKind::Vectorscope), "Vectorscope", "Input / Output" });
    entries.push_back({ EditorNodeGraph::NodeKind::Scope, static_cast<int>(EditorNodeGraph::ScopeKind::RGBParade), "RGB Parade", "Input / Output" });
    entries.push_back({ EditorNodeGraph::NodeKind::Preview, 0, "Preview", "Input / Output" });
    entries.push_back({ EditorNodeGraph::NodeKind::MaskGenerator, static_cast<int>(EditorNodeGraph::MaskGeneratorKind::Solid), "Solid Mask", "Masks" });
    entries.push_back({ EditorNodeGraph::NodeKind::MaskGenerator, static_cast<int>(EditorNodeGraph::MaskGeneratorKind::LinearGradient), "Linear Gradient Mask", "Masks" });
    entries.push_back({ EditorNodeGraph::NodeKind::MaskGenerator, static_cast<int>(EditorNodeGraph::MaskGeneratorKind::RadialGradient), "Radial Gradient Mask", "Masks" });
    entries.push_back({ EditorNodeGraph::NodeKind::MaskGenerator, static_cast<int>(EditorNodeGraph::MaskGeneratorKind::Noise), "Noise Mask", "Masks" });
    entries.push_back({ EditorNodeGraph::NodeKind::MaskUtility, static_cast<int>(EditorNodeGraph::MaskUtilityKind::Invert), "Invert Mask", "Masks" });
    entries.push_back({ EditorNodeGraph::NodeKind::MaskUtility, static_cast<int>(EditorNodeGraph::MaskUtilityKind::Levels), "Levels Mask", "Masks" });
    entries.push_back({ EditorNodeGraph::NodeKind::MaskUtility, static_cast<int>(EditorNodeGraph::MaskUtilityKind::Threshold), "Threshold Mask", "Masks" });
    entries.push_back({ EditorNodeGraph::NodeKind::ImageToMask, static_cast<int>(EditorNodeGraph::ImageToMaskKind::Luminance), "Luminance Mask", "Masks" });
    entries.push_back({ EditorNodeGraph::NodeKind::ImageGenerator, static_cast<int>(EditorNodeGraph::ImageGeneratorKind::SolidColor), "Solid Color Image", "Texture / Generate" });
    entries.push_back({ EditorNodeGraph::NodeKind::ImageGenerator, static_cast<int>(EditorNodeGraph::ImageGeneratorKind::ColorGradient), "Color Gradient Image", "Texture / Generate" });
    entries.push_back({ EditorNodeGraph::NodeKind::ImageGenerator, static_cast<int>(EditorNodeGraph::ImageGeneratorKind::Square), "Square", "Texture / Generate" });
    entries.push_back({ EditorNodeGraph::NodeKind::ImageGenerator, static_cast<int>(EditorNodeGraph::ImageGeneratorKind::Circle), "Circle", "Texture / Generate" });
    entries.push_back({ EditorNodeGraph::NodeKind::ImageGenerator, static_cast<int>(EditorNodeGraph::ImageGeneratorKind::Text), "Text", "Texture / Generate" });
    entries.push_back({ EditorNodeGraph::NodeKind::Mix, 0, "Blend", "Composite" });
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
        case EditorNodeGraph::NodeKind::MaskUtility:
            node.maskUtilityKind = static_cast<EditorNodeGraph::MaskUtilityKind>(entry.value);
            break;
        case EditorNodeGraph::NodeKind::ImageToMask:
            node.imageToMaskKind = static_cast<EditorNodeGraph::ImageToMaskKind>(entry.value);
            break;
        case EditorNodeGraph::NodeKind::ImageGenerator:
            node.imageGeneratorKind = static_cast<EditorNodeGraph::ImageGeneratorKind>(entry.value);
            break;
        default:
            break;
    }
    ApplyNodeMetadata(node);
    return node;
}

} // namespace EditorNodeGraphDefinitions
