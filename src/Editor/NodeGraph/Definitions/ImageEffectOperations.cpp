#include "Editor/NodeGraph/Definitions/OperationInventory.h"
#include "Editor/LayerRegistry.h"

namespace EditorNodeGraphDefinitions {
namespace OperationDefinitions {
using EditorNodeGraph::Node;
using EditorNodeGraph::NodeKind;
using Stack::NodeMath::LogicalValueType;
using LogicalValue = LogicalValueType;
using TechnicalOperation = Stack::NodeMath::TechnicalImageOperation;
void AppendImageEffectOperations(std::vector<OperationRegistration>& operations) {
    const auto add = [&](LiveNodeDefinition definition) {
        definition.bypassBindings = {{"imageOut", "imageIn"}};
        if (definition.kind == NodeKind::Mix) definition.bypassBindings = {{"imageOut", "imageA"}, {"imageOut", "imageB"}};
        if (definition.kind == NodeKind::RawOperation) {
            definition.requiresSceneLinearRgb = definition.variant!=static_cast<int>(Stack::RawRecipe::GraphOperationKind::ToneCurve) &&
                definition.variant!=static_cast<int>(Stack::RawRecipe::GraphOperationKind::Exposure);
            definition.outputDependencies.push_back({"inputImageOut", {"imageIn"}});
        }
        operations.push_back({OperationFamily::ImageEffects, std::move(definition)});
    };

    for (int i = 0; i < static_cast<int>(Stack::RawRecipe::GraphOperationKind::Count); ++i)
        add(MakeOperation(EditorNodeGraph::NodeKind::RawOperation, i, Stack::RawRecipe::GraphOperationLabel(static_cast<Stack::RawRecipe::GraphOperationKind>(i)), "Image Operations", std::string("raw-operation:") + Stack::RawRecipe::GraphOperationId(static_cast<Stack::RawRecipe::GraphOperationKind>(i))));

    for (const LayerDescriptor& descriptor : LayerRegistry::GetAllDescriptors()) {
        const bool visible = LayerRegistry::ShouldShowInNodeBrowser(descriptor);
        std::string label = descriptor.displayName ? descriptor.displayName : "Layer";
        if (visible && descriptor.lifecycleStatus == LayerLifecycleStatus::Experimental) {
            label += " (Experimental)";
        } else if (visible && descriptor.lifecycleStatus == LayerLifecycleStatus::NeedsFix) {
            label += " (Needs Fix)";
        }
        add(MakeOperation(EditorNodeGraph::NodeKind::Layer, static_cast<int>(descriptor.type), std::move(label), descriptor.categoryName ? descriptor.categoryName : "Layers", std::string("layer:") + descriptor.typeId, visible ? NodeCatalogPreviewStrategy::Auto : NodeCatalogPreviewStrategy::NoPreview, 1, visible));
    }
    add(MakeOperation(EditorNodeGraph::NodeKind::Reformat, 0, "Framing", "Geometry / Transform", "geometry:reformat", NodeCatalogPreviewStrategy::FallbackOnly));
    using TechnicalOperation = Stack::NodeMath::TechnicalImageOperation;
    add(MakeOperation(EditorNodeGraph::NodeKind::TechnicalImage, static_cast<int>(TechnicalOperation::AssignSrgb), "Assign sRGB", "Image Technical", std::string("technical-image:") + Stack::NodeMath::TechnicalOperationIdentity(TechnicalOperation::AssignSrgb)));
    add(MakeOperation(EditorNodeGraph::NodeKind::TechnicalImage, static_cast<int>(TechnicalOperation::AssignLinearSrgb), "Assign Linear sRGB", "Image Technical", std::string("technical-image:") + Stack::NodeMath::TechnicalOperationIdentity(TechnicalOperation::AssignLinearSrgb)));
    add(MakeOperation(EditorNodeGraph::NodeKind::TechnicalImage, static_cast<int>(TechnicalOperation::AssignLinearDisplayP3), "Assign Linear Display-P3", "Image Technical", std::string("technical-image:") + Stack::NodeMath::TechnicalOperationIdentity(TechnicalOperation::AssignLinearDisplayP3)));
    add(MakeOperation(EditorNodeGraph::NodeKind::TechnicalImage, static_cast<int>(TechnicalOperation::SrgbDecode), "sRGB Decode", "Image Technical", std::string("technical-image:") + Stack::NodeMath::TechnicalOperationIdentity(TechnicalOperation::SrgbDecode)));
    add(MakeOperation(EditorNodeGraph::NodeKind::TechnicalImage, static_cast<int>(TechnicalOperation::SrgbEncode), "sRGB Encode", "Image Technical", std::string("technical-image:") + Stack::NodeMath::TechnicalOperationIdentity(TechnicalOperation::SrgbEncode)));
    add(MakeOperation(EditorNodeGraph::NodeKind::TechnicalImage, static_cast<int>(TechnicalOperation::LinearSrgbToDisplayP3), "Linear sRGB to Display-P3", "Image Technical", std::string("technical-image:") + Stack::NodeMath::TechnicalOperationIdentity(TechnicalOperation::LinearSrgbToDisplayP3)));
    add(MakeOperation(EditorNodeGraph::NodeKind::TechnicalImage, static_cast<int>(TechnicalOperation::LinearDisplayP3ToSrgb), "Linear Display-P3 to sRGB", "Image Technical", std::string("technical-image:") + Stack::NodeMath::TechnicalOperationIdentity(TechnicalOperation::LinearDisplayP3ToSrgb)));
    add(MakeOperation(EditorNodeGraph::NodeKind::TechnicalImage, static_cast<int>(TechnicalOperation::Exposure), "Exposure (EV)", "Image Technical", std::string("technical-image:") + Stack::NodeMath::TechnicalOperationIdentity(TechnicalOperation::Exposure)));
    add(MakeOperation(EditorNodeGraph::NodeKind::TechnicalImage, static_cast<int>(TechnicalOperation::Premultiply), "Premultiply", "Image Technical", std::string("technical-image:") + Stack::NodeMath::TechnicalOperationIdentity(TechnicalOperation::Premultiply)));
    add(MakeOperation(EditorNodeGraph::NodeKind::TechnicalImage, static_cast<int>(TechnicalOperation::Unpremultiply), "Unpremultiply", "Image Technical", std::string("technical-image:") + Stack::NodeMath::TechnicalOperationIdentity(TechnicalOperation::Unpremultiply)));
    add(MakeOperation(EditorNodeGraph::NodeKind::Mix, 0, "Blend Images", "Image Operations", "blend-images"));
    add(MakeOperation(EditorNodeGraph::NodeKind::Lut, 0, "LUT", "Image Operations", "lut"));
}
} // namespace OperationDefinitions
} // namespace EditorNodeGraphDefinitions
