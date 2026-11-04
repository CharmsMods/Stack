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
            const int inputs = Stack::NodeMath::Primitive(static_cast<EditorNodeGraph::DataMathMode>(definition.variant))
                ? 2 : EditorNodeGraph::kMaxDataMathInputCount;
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
    add(MakeOperation(EditorNodeGraph::NodeKind::DataMath, static_cast<int>(EditorNodeGraph::DataMathMode::Average), "Average", "Math", "data-math:average"));
    add(MakeOperation(EditorNodeGraph::NodeKind::DataMath, static_cast<int>(EditorNodeGraph::DataMathMode::Min), "Minimum", "Math", "data-math:min"));
    add(MakeOperation(EditorNodeGraph::NodeKind::DataMath, static_cast<int>(EditorNodeGraph::DataMathMode::Max), "Maximum", "Math", "data-math:max"));
    add(MakeOperation(EditorNodeGraph::NodeKind::DataMath, static_cast<int>(EditorNodeGraph::DataMathMode::Difference), "Difference", "Math", "data-math:difference"));
    add(MakeOperation(EditorNodeGraph::NodeKind::DataMath, static_cast<int>(EditorNodeGraph::DataMathMode::Remap), "Remap", "Math", "data-math:remap"));
    add(MakeOperation(EditorNodeGraph::NodeKind::DataMath, static_cast<int>(EditorNodeGraph::DataMathMode::ImageAverage), "Average Images", "Image Operations", "data-math:image-average"));
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
