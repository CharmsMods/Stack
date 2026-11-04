#include "Editor/NodeGraph/Definitions/OperationInventory.h"

namespace EditorNodeGraphDefinitions::OperationDefinitions {

// RAW owns creation of these operations through its masking tools and layer
// references. They have execution contracts but no general graph add command.
void AppendRawMaskOperations(std::vector<OperationRegistration>& operations) {
    using EditorNodeGraph::NodeKind;
    const auto add = [&](LiveNodeDefinition definition) {
        definition.visibleInBrowser = false;
        definition.graphRoles = static_cast<unsigned>(LiveGraphRole::RawLayer);
        if (definition.kind == NodeKind::MaskUtility)
            definition.bypassBindings = {{"maskOut", "maskIn"}};
        if (definition.kind == NodeKind::MaskCombine)
            definition.bypassBindings = {{"maskOut", "maskA"}, {"maskOut", "maskB"}};
        operations.push_back({OperationFamily::RawMasking, std::move(definition)});
    };
    for (auto shape : {EditorNodeGraph::MaskGeneratorKind::LinearGradient, EditorNodeGraph::MaskGeneratorKind::RadialGradient, EditorNodeGraph::MaskGeneratorKind::Square}) {
        const char* title = shape == EditorNodeGraph::MaskGeneratorKind::LinearGradient ? "Linear mask texture" :
            shape == EditorNodeGraph::MaskGeneratorKind::RadialGradient ? "Radial mask texture" : "Square mask texture";
        add(MakeOperation(EditorNodeGraph::NodeKind::MaskTexture, static_cast<int>(shape), title, "Masks", std::string("source:mask-texture:") + std::to_string(static_cast<int>(shape))));
    }
    add(MakeOperation(EditorNodeGraph::NodeKind::MaskGenerator, static_cast<int>(EditorNodeGraph::MaskGeneratorKind::Solid), "Solid Mask", "Masks", "mask:solid"));
    add(MakeOperation(EditorNodeGraph::NodeKind::MaskGenerator, static_cast<int>(EditorNodeGraph::MaskGeneratorKind::LinearGradient), "Linear Gradient Mask", "Masks", "mask:linear-gradient"));
    add(MakeOperation(EditorNodeGraph::NodeKind::MaskGenerator, static_cast<int>(EditorNodeGraph::MaskGeneratorKind::RadialGradient), "Radial Gradient Mask", "Masks", "mask:radial-gradient"));
    add(MakeOperation(EditorNodeGraph::NodeKind::MaskGenerator, static_cast<int>(EditorNodeGraph::MaskGeneratorKind::Noise), "Noise Mask", "Masks", "mask:noise"));
    add(MakeOperation(EditorNodeGraph::NodeKind::MaskGenerator, static_cast<int>(EditorNodeGraph::MaskGeneratorKind::Square), "Square Mask", "Masks", "mask:square"));
    add(MakeOperation(EditorNodeGraph::NodeKind::MaskGenerator, static_cast<int>(EditorNodeGraph::MaskGeneratorKind::RawGradient), "Photo gradient mask", "Masks", "mask:photo-gradient"));
    add(MakeOperation(EditorNodeGraph::NodeKind::MaskGenerator, static_cast<int>(EditorNodeGraph::MaskGeneratorKind::PaintedArea), "Painted area mask", "Masks", "mask:painted-area"));
    // Populated mask assets retain this identity; blank creation has no editor.
    add(MakeOperation(EditorNodeGraph::NodeKind::CustomMask, 0, "Custom Mask", "Masks", "custom-mask", NodeCatalogPreviewStrategy::FallbackOnly));
    add(MakeOperation(EditorNodeGraph::NodeKind::MaskCombine, static_cast<int>(EditorNodeGraph::MaskCombineMode::Add), "Add Mask", "Mask / Math", "mask-combine:add"));
    add(MakeOperation(EditorNodeGraph::NodeKind::MaskCombine, static_cast<int>(EditorNodeGraph::MaskCombineMode::Subtract), "Subtract Mask", "Mask / Math", "mask-combine:subtract"));
    add(MakeOperation(EditorNodeGraph::NodeKind::MaskCombine, static_cast<int>(EditorNodeGraph::MaskCombineMode::Intersect), "Intersect Mask", "Mask / Math", "mask-combine:intersect"));
    add(MakeOperation(EditorNodeGraph::NodeKind::MaskCombine, static_cast<int>(EditorNodeGraph::MaskCombineMode::Exclude), "Difference Mask", "Mask / Math", "mask-combine:exclude"));
    add(MakeOperation(EditorNodeGraph::NodeKind::MaskUtility, static_cast<int>(EditorNodeGraph::MaskUtilityKind::Invert), "Invert Mask", "Mask / Math", "mask-utility:invert"));
    add(MakeOperation(EditorNodeGraph::NodeKind::MaskUtility, static_cast<int>(EditorNodeGraph::MaskUtilityKind::Levels), "Remap Mask", "Mask / Math", "mask-utility:levels"));
    add(MakeOperation(EditorNodeGraph::NodeKind::MaskUtility, static_cast<int>(EditorNodeGraph::MaskUtilityKind::Threshold), "Threshold Mask", "Mask / Math", "mask-utility:threshold"));
    add(MakeOperation(EditorNodeGraph::NodeKind::ImageToMask, static_cast<int>(EditorNodeGraph::ImageToMaskKind::Luminance), "Luminance Mask", "Mask / Math", "image-to-mask:luminance"));
    add(MakeOperation(EditorNodeGraph::NodeKind::ImageToMask, static_cast<int>(EditorNodeGraph::ImageToMaskKind::SampledRange), "Sampled Range Mask", "Mask / Math", "image-to-mask:sampled-range"));
}

} // namespace EditorNodeGraphDefinitions::OperationDefinitions
