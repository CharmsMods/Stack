#include "Editor/NodeGraph/Definitions/OperationInventory.h"

namespace EditorNodeGraphDefinitions {
namespace OperationDefinitions {
using EditorNodeGraph::Node;
using EditorNodeGraph::NodeKind;
using Stack::NodeMath::LogicalValueType;
using LogicalValue = LogicalValueType;
using TechnicalOperation = Stack::NodeMath::TechnicalImageOperation;
void AppendFieldOperations(std::vector<OperationRegistration>& operations) {
    const auto add = [&](LiveNodeDefinition definition) {
        if (definition.kind == NodeKind::DataMath) {
            const auto mode = static_cast<EditorNodeGraph::DataMathMode>(definition.variant);
            const int inputs = Stack::NodeMath::ArithmeticInputCount(mode);
            if (mode == EditorNodeGraph::DataMathMode::Mix || mode == EditorNodeGraph::DataMathMode::Remap) {
                definition.inspectability = Stack::NodeMath::Inspectability::TransparentGraph;
                definition.canonicalRecipeId = mode == EditorNodeGraph::DataMathMode::Mix ? "stack:recipe/mix" : "stack:recipe/remap-range";
                definition.decompositionReason.clear();
            }
            for (int i=0; i<inputs; ++i)
                definition.bypassBindings.push_back({"imageOut", EditorNodeGraph::DataMathInputSocketId(i)});
        }
        operations.push_back({OperationFamily::FieldsAndValues, std::move(definition)});
    };
    add(MakeOperation(EditorNodeGraph::NodeKind::DataMath, static_cast<int>(EditorNodeGraph::DataMathMode::Clamp), "Clamp", "Math", "data-math:clamp"));
    add(MakeOperation(EditorNodeGraph::NodeKind::DataMath, static_cast<int>(EditorNodeGraph::DataMathMode::Add), "Add", "Math", "data-math:add"));
    add(MakeOperation(EditorNodeGraph::NodeKind::DataMath, static_cast<int>(EditorNodeGraph::DataMathMode::Subtract), "Subtract", "Math", "data-math:subtract"));
    add(MakeOperation(EditorNodeGraph::NodeKind::DataMath, static_cast<int>(EditorNodeGraph::DataMathMode::Multiply), "Multiply", "Math", "data-math:multiply"));
    add(MakeOperation(EditorNodeGraph::NodeKind::DataMath, static_cast<int>(EditorNodeGraph::DataMathMode::Divide), "Divide", "Math", "data-math:divide"));
    add(MakeOperation(EditorNodeGraph::NodeKind::DataMath, static_cast<int>(EditorNodeGraph::DataMathMode::Mix), "Mix", "Math", "data-math:mix", NodeCatalogPreviewStrategy::FallbackOnly));
    operations.back().definition.searchAliases = "average|mean|weighted average|blend";
    add(MakeOperation(EditorNodeGraph::NodeKind::DataMath, static_cast<int>(EditorNodeGraph::DataMathMode::Min), "Minimum", "Math", "data-math:min", NodeCatalogPreviewStrategy::FallbackOnly, 1, false));
    add(MakeOperation(EditorNodeGraph::NodeKind::DataMath, static_cast<int>(EditorNodeGraph::DataMathMode::Max), "Maximum", "Math", "data-math:max", NodeCatalogPreviewStrategy::FallbackOnly, 1, false));
    add(MakeOperation(EditorNodeGraph::NodeKind::DataMath, static_cast<int>(EditorNodeGraph::DataMathMode::Difference), "Absolute Difference", "Math", "data-math:difference"));
    add(MakeOperation(EditorNodeGraph::NodeKind::DataMath, static_cast<int>(EditorNodeGraph::DataMathMode::Remap), "Remap Range", "Math", "data-math:remap"));
    add(MakeOperation(NodeKind::Value, static_cast<int>(LogicalValue::Scalar), "Number", "Math", "number", NodeCatalogPreviewStrategy::FallbackOnly));
    add(MakeOperation(EditorNodeGraph::NodeKind::Value, static_cast<int>(LogicalValue::Curve1D), "Curve Value", "Values", std::string("value:") + Stack::NodeMath::SerializeFirstClassValue(BuildDefaultFirstClassValue(LogicalValue::Curve1D)).value("logicalType", "invalid"), NodeCatalogPreviewStrategy::FallbackOnly));
    // Imported first-class resources and unavailable values use the same exact
    // type identities as authored literals. They have no blank browser editor.
    // Register them even when their runtime resource is not currently available.
    for (int variant=static_cast<int>(LogicalValue::Lut);
         variant<static_cast<int>(LogicalValue::Failure); ++variant) {
        const auto value=BuildDefaultFirstClassValue(static_cast<LogicalValue>(variant));
        const auto type=Stack::NodeMath::SerializeFirstClassValue(value).value("logicalType", "invalid");
        add(MakeOperation(NodeKind::Value, variant, "Typed " + type + " Value", "Values",
            "value:" + type, NodeCatalogPreviewStrategy::NoPreview, 1, false));
    }
    add(MakeOperation(EditorNodeGraph::NodeKind::FieldMean, 0, "Field Mean", "Analysis / Measure", "analysis:field-mean", NodeCatalogPreviewStrategy::FallbackOnly));
    operations.back().definition.searchAliases = "average pixels|mean of a channel";
    add(MakeOperation(EditorNodeGraph::NodeKind::ImageGenerator, static_cast<int>(EditorNodeGraph::ImageGeneratorKind::SolidColor), "Solid Color Image", "Texture / Generate", "image-generator:solid-color"));
    add(MakeOperation(EditorNodeGraph::NodeKind::ImageGenerator, static_cast<int>(EditorNodeGraph::ImageGeneratorKind::ColorGradient), "Color Gradient Image", "Texture / Generate", "image-generator:color-gradient"));
    add(MakeOperation(EditorNodeGraph::NodeKind::ImageGenerator, static_cast<int>(EditorNodeGraph::ImageGeneratorKind::Square), "Square", "Texture / Generate", "image-generator:square"));
    add(MakeOperation(EditorNodeGraph::NodeKind::ImageGenerator, static_cast<int>(EditorNodeGraph::ImageGeneratorKind::Circle), "Circle", "Texture / Generate", "image-generator:circle"));
    add(MakeOperation(EditorNodeGraph::NodeKind::ImageGenerator, static_cast<int>(EditorNodeGraph::ImageGeneratorKind::Text), "Text", "Texture / Generate", "image-generator:text"));
    add(MakeOperation(EditorNodeGraph::NodeKind::ChannelSplit, 0, "Channel Split", "Channels", "channel-split"));
    add(MakeOperation(EditorNodeGraph::NodeKind::ChannelCombine, 0, "Image Combine", "Channels", "channel-combine"));
    operations.back().definition.searchAliases = "Channel Combine RGBA";
    add(MakeOperation(EditorNodeGraph::NodeKind::ConstantChannel, 0, "Constant Channel", "Channels", "constant-channel", NodeCatalogPreviewStrategy::FallbackOnly));
}
} // namespace OperationDefinitions
} // namespace EditorNodeGraphDefinitions
