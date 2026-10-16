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
        switch (definition.kind) {
            case NodeKind::MaskTexture:
                definition.graphRoles = static_cast<unsigned>(LiveGraphRole::RawLayer); break;
            case NodeKind::MaskUtility:
                definition.bypassBindings = {{"maskOut", "maskIn"}}; break;
            case NodeKind::MaskCombine:
                definition.bypassBindings = {{"maskOut", "maskA"}, {"maskOut", "maskB"}}; break;
            case NodeKind::DataMath:
                for (int i=0; i<EditorNodeGraph::kMaxDataMathInputCount; ++i)
                    definition.bypassBindings.push_back({"imageOut", EditorNodeGraph::DataMathInputSocketId(i)});
                break;
            default: break;
        }
        operations.push_back({OperationFamily::FieldsAndValues, std::move(definition)});
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
    add(MakeOperation(EditorNodeGraph::NodeKind::DataMath, static_cast<int>(EditorNodeGraph::DataMathMode::Clamp), "Clamp", "Mask / Math", "data-math:clamp"));
    add(MakeOperation(EditorNodeGraph::NodeKind::DataMath, static_cast<int>(EditorNodeGraph::DataMathMode::Add), "Add", "Mask / Math", "data-math:add"));
    add(MakeOperation(EditorNodeGraph::NodeKind::DataMath, static_cast<int>(EditorNodeGraph::DataMathMode::Subtract), "Subtract", "Mask / Math", "data-math:subtract"));
    add(MakeOperation(EditorNodeGraph::NodeKind::DataMath, static_cast<int>(EditorNodeGraph::DataMathMode::Multiply), "Multiply", "Mask / Math", "data-math:multiply"));
    add(MakeOperation(EditorNodeGraph::NodeKind::DataMath, static_cast<int>(EditorNodeGraph::DataMathMode::Divide), "Divide", "Mask / Math", "data-math:divide"));
    add(MakeOperation(EditorNodeGraph::NodeKind::DataMath, static_cast<int>(EditorNodeGraph::DataMathMode::Average), "Average", "Mask / Math", "data-math:average"));
    add(MakeOperation(EditorNodeGraph::NodeKind::DataMath, static_cast<int>(EditorNodeGraph::DataMathMode::Min), "Minimum", "Mask / Math", "data-math:min"));
    add(MakeOperation(EditorNodeGraph::NodeKind::DataMath, static_cast<int>(EditorNodeGraph::DataMathMode::Max), "Maximum", "Mask / Math", "data-math:max"));
    add(MakeOperation(EditorNodeGraph::NodeKind::DataMath, static_cast<int>(EditorNodeGraph::DataMathMode::Difference), "Difference", "Mask / Math", "data-math:difference"));
    add(MakeOperation(EditorNodeGraph::NodeKind::DataMath, static_cast<int>(EditorNodeGraph::DataMathMode::Remap), "Remap", "Mask / Math", "data-math:remap"));
    add(MakeOperation(EditorNodeGraph::NodeKind::DataMath, static_cast<int>(EditorNodeGraph::DataMathMode::ImageAverage), "Average Images", "Image Operations", "data-math:image-average"));
    add(MakeOperation(EditorNodeGraph::NodeKind::Value, static_cast<int>(LogicalValue::Boolean), "Boolean Value", "Values", std::string("value:") + Stack::NodeMath::SerializeFirstClassValue(BuildDefaultFirstClassValue(LogicalValue::Boolean)).value("logicalType", "invalid"), NodeCatalogPreviewStrategy::FallbackOnly));
    add(MakeOperation(EditorNodeGraph::NodeKind::Value, static_cast<int>(LogicalValue::Integer), "Integer Value", "Values", std::string("value:") + Stack::NodeMath::SerializeFirstClassValue(BuildDefaultFirstClassValue(LogicalValue::Integer)).value("logicalType", "invalid"), NodeCatalogPreviewStrategy::FallbackOnly));
    add(MakeOperation(EditorNodeGraph::NodeKind::Value, static_cast<int>(LogicalValue::Scalar), "Scalar Value", "Values", std::string("value:") + Stack::NodeMath::SerializeFirstClassValue(BuildDefaultFirstClassValue(LogicalValue::Scalar)).value("logicalType", "invalid"), NodeCatalogPreviewStrategy::FallbackOnly));
    add(MakeOperation(EditorNodeGraph::NodeKind::Value, static_cast<int>(LogicalValue::Vector2), "Vector 2 Value", "Values", std::string("value:") + Stack::NodeMath::SerializeFirstClassValue(BuildDefaultFirstClassValue(LogicalValue::Vector2)).value("logicalType", "invalid"), NodeCatalogPreviewStrategy::FallbackOnly));
    add(MakeOperation(EditorNodeGraph::NodeKind::Value, static_cast<int>(LogicalValue::Vector3), "Vector 3 Value", "Values", std::string("value:") + Stack::NodeMath::SerializeFirstClassValue(BuildDefaultFirstClassValue(LogicalValue::Vector3)).value("logicalType", "invalid"), NodeCatalogPreviewStrategy::FallbackOnly));
    add(MakeOperation(EditorNodeGraph::NodeKind::Value, static_cast<int>(LogicalValue::Vector4), "Vector 4 Value", "Values", std::string("value:") + Stack::NodeMath::SerializeFirstClassValue(BuildDefaultFirstClassValue(LogicalValue::Vector4)).value("logicalType", "invalid"), NodeCatalogPreviewStrategy::FallbackOnly));
    add(MakeOperation(EditorNodeGraph::NodeKind::Value, static_cast<int>(LogicalValue::Matrix3), "Matrix 3x3 Value", "Values", std::string("value:") + Stack::NodeMath::SerializeFirstClassValue(BuildDefaultFirstClassValue(LogicalValue::Matrix3)).value("logicalType", "invalid"), NodeCatalogPreviewStrategy::FallbackOnly));
    add(MakeOperation(EditorNodeGraph::NodeKind::Value, static_cast<int>(LogicalValue::Matrix4), "Matrix 4x4 Value", "Values", std::string("value:") + Stack::NodeMath::SerializeFirstClassValue(BuildDefaultFirstClassValue(LogicalValue::Matrix4)).value("logicalType", "invalid"), NodeCatalogPreviewStrategy::FallbackOnly));
    add(MakeOperation(EditorNodeGraph::NodeKind::Value, static_cast<int>(LogicalValue::Coordinate2), "Coordinate Value", "Values", std::string("value:") + Stack::NodeMath::SerializeFirstClassValue(BuildDefaultFirstClassValue(LogicalValue::Coordinate2)).value("logicalType", "invalid"), NodeCatalogPreviewStrategy::FallbackOnly));
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
