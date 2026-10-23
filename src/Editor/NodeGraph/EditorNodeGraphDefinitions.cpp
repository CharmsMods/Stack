#include "EditorNodeGraphDefinitions.h"

#include "Editor/LayerRegistry.h"
#include "Editor/NodeGraph/UnifiedNodeDefinitionRegistry.h"
#include "Editor/NodeGraph/Definitions/OperationInventory.h"
#include "Graph/LayerOperationParameters.h"

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
        case EditorNodeGraph::MaskGeneratorKind::Square: return "Square Mask";
        case EditorNodeGraph::MaskGeneratorKind::RawGradient: return "Photo gradient mask";
        case EditorNodeGraph::MaskGeneratorKind::PaintedArea: return "Painted area mask";
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
        case EditorNodeGraph::DataMathMode::ImageAverage: return "Average Images";
    }
    return "Math";
}

const char* ValueTitle(Stack::NodeMath::LogicalValueType type) {
    using Type = Stack::NodeMath::LogicalValueType;
    switch (type) {
        case Type::Boolean: return "Boolean Value";
        case Type::Integer: return "Integer Value";
        case Type::Scalar: return "Scalar Value";
        case Type::Vector2: return "Vector 2 Value";
        case Type::Vector3: return "Vector 3 Value";
        case Type::Vector4: return "Vector 4 Value";
        case Type::Matrix3: return "Matrix 3x3 Value";
        case Type::Matrix4: return "Matrix 4x4 Value";
        case Type::Coordinate2: return "Coordinate Value";
        case Type::Curve1D: return "Curve Value";
        default: return "Typed Value";
    }
}

EditorNodeGraph::SocketType ValueSocketType(Stack::NodeMath::LogicalValueType type) {
    using Logical = Stack::NodeMath::LogicalValueType;
    using Socket = EditorNodeGraph::SocketType;
    switch (type) {
        case Logical::Boolean: return Socket::Boolean;
        case Logical::Integer: return Socket::Integer;
        case Logical::Scalar: return Socket::Scalar;
        case Logical::Vector2: return Socket::Vector2;
        case Logical::Vector3: return Socket::Vector3;
        case Logical::Vector4: return Socket::Vector4;
        case Logical::Matrix3: return Socket::Matrix3;
        case Logical::Matrix4: return Socket::Matrix4;
        case Logical::Coordinate2: return Socket::Coordinate;
        case Logical::Curve1D: return Socket::Curve;
        case Logical::Histogram: return Socket::Histogram;
        case Logical::Statistics: return Socket::Statistics;
        case Logical::Metadata: return Socket::Metadata;
        case Logical::SpecializedHandle: return Socket::Handle;
        case Logical::ScalarField: return Socket::ScalarField;
        default: return Socket::Value;
    }
}

Stack::NodeMath::FirstClassValue DefaultValue(Stack::NodeMath::LogicalValueType type) {
    using namespace Stack::NodeMath;
    FirstClassValue value;
    value.logicalType = type;
    value.storage = type == LogicalValueType::Curve1D
        ? ValueStorageClass::StructuredResource
        : ValueStorageClass::Uniform;
    switch (type) {
        case LogicalValueType::Channel: case LogicalValueType::ScalarField:
        case LogicalValueType::Vector2Field: case LogicalValueType::Vector3Field:
        case LogicalValueType::Vector4Field: case LogicalValueType::ColorImage:
        case LogicalValueType::Mask: case LogicalValueType::DataImage:
            value.storage = ValueStorageClass::PerPixelField; break;
        case LogicalValueType::Lut: case LogicalValueType::Histogram:
        case LogicalValueType::Statistics: case LogicalValueType::Metadata:
            value.storage = ValueStorageClass::StructuredResource; break;
        case LogicalValueType::SpecializedHandle: case LogicalValueType::Raw:
        case LogicalValueType::Analysis:
            value.storage = ValueStorageClass::SpecializedHandle; break;
        default: break;
    }
    value.availability = ValueAvailability::Known;
    switch (type) {
        case LogicalValueType::Boolean: value.payload = false; break;
        case LogicalValueType::Integer: value.payload = std::int64_t{ 0 }; break;
        case LogicalValueType::Scalar: value.payload = 0.0; break;
        case LogicalValueType::Vector2:
        case LogicalValueType::Coordinate2: value.payload = std::array<double, 2>{ 0.0, 0.0 }; break;
        case LogicalValueType::Vector3: value.payload = std::array<double, 3>{ 0.0, 0.0, 0.0 }; break;
        case LogicalValueType::Vector4: value.payload = std::array<double, 4>{ 0.0, 0.0, 0.0, 0.0 }; break;
        case LogicalValueType::Matrix3: value.payload = std::array<double, 9>{ 1,0,0,0,1,0,0,0,1 }; break;
        case LogicalValueType::Matrix4: value.payload = std::array<double, 16>{ 1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1 }; break;
        case LogicalValueType::Curve1D: value.payload = CurveValue{ { {0.0, 0.0}, {1.0, 1.0} }, "linear", "clamp" }; break;
        default: value.availability = ValueAvailability::Unknown; value.payload = std::monostate{}; break;
    }
    return value;
}

const char* TechnicalImageTitle(Stack::NodeMath::TechnicalImageOperation operation) {
    using Operation = Stack::NodeMath::TechnicalImageOperation;
    switch (operation) {
        case Operation::AssignSrgb: return "Assign sRGB";
        case Operation::AssignLinearSrgb: return "Assign Linear sRGB";
        case Operation::AssignLinearDisplayP3: return "Assign Linear Display-P3";
        case Operation::SrgbDecode: return "sRGB Decode";
        case Operation::SrgbEncode: return "sRGB Encode";
        case Operation::LinearSrgbToDisplayP3: return "Linear sRGB to Display-P3";
        case Operation::LinearDisplayP3ToSrgb: return "Linear Display-P3 to sRGB";
        case Operation::Exposure: return "Exposure (EV)";
        case Operation::Premultiply: return "Premultiply";
        case Operation::Unpremultiply: return "Unpremultiply";
    }
    return "Technical Image";
}











} // namespace

void ApplyNodeMetadata(EditorNodeGraph::Node& node) {
    if (node.instanceUuid.empty()) {
        node.instanceUuid = Stack::NodeMath::GenerateCanonicalUuid();
    }
    switch (node.kind) {
        case EditorNodeGraph::NodeKind::Image:
            node.title = node.image.label.empty() ? "Slice" : node.image.label;
            break;
        case EditorNodeGraph::NodeKind::RawSource:
            node.title = node.rawSource.label.empty() ? "RAW" : node.rawSource.label;
            break;
        case EditorNodeGraph::NodeKind::RawDevelopment:
            node.title = "RAW Development";
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
        case EditorNodeGraph::NodeKind::Mfsr:
            node.title = "MFSR";
            break;
        case EditorNodeGraph::NodeKind::RawProjectFrame:
            node.title = node.rawProjectFrame.displayLabel.empty()
                ? "RAW Frame"
                : node.rawProjectFrame.displayLabel;
            node.expanded = true;
            break;
        case EditorNodeGraph::NodeKind::MultiFrameDenoise:
            node.title = "MFD";
            node.expanded = true;
            break;
        case EditorNodeGraph::NodeKind::MultiFrameHdr:
            node.title = node.multiFrameHdr.bracketingSource
                ? "Bracketing" : "Multi-Frame HDR";
            node.expanded = true;
            break;
        case EditorNodeGraph::NodeKind::RawProjectSourceSet:
            node.title = "RAW Project Source Set";
            node.expanded = false;
            break;
        case EditorNodeGraph::NodeKind::Lut:
            node.title = "LUT";
            break;
        case EditorNodeGraph::NodeKind::RawOperation:
            node.title = Stack::RawRecipe::GraphOperationLabel(node.rawOperation.kind);
            node.expanded = true;
            break;
        case EditorNodeGraph::NodeKind::Layer: {
            const LayerDescriptor* descriptor = LayerRegistry::GetDescriptor(node.layerType);
            node.typeId = descriptor ? descriptor->typeId : "";
            if (node.layerParameters.empty())
                node.layerParameters=Stack::GraphModel::DefaultLayerParameters(node.layerType);
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
        case EditorNodeGraph::NodeKind::MaskTexture:
            node.title = node.maskKind == EditorNodeGraph::MaskGeneratorKind::LinearGradient ? "Linear mask texture" :
                node.maskKind == EditorNodeGraph::MaskGeneratorKind::RadialGradient ? "Radial mask texture" : "Square mask texture";
            node.rawCoverage = {{"canvas","project"}};
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
            node.title = "Image Combine";
            break;
        case EditorNodeGraph::NodeKind::ConstantChannel:
            node.title = "Constant Channel";
            break;
        case EditorNodeGraph::NodeKind::DataMath:
            node.title = DataMathTitle(node.dataMathMode);
            break;
        case EditorNodeGraph::NodeKind::Value:
            node.title = ValueTitle(node.value.value.logicalType);
            break;
        case EditorNodeGraph::NodeKind::FieldMean:
            node.title = "Field Mean";
            break;
        case EditorNodeGraph::NodeKind::Reformat:
            node.title = "Framing";
            node.expanded = true;
            break;
        case EditorNodeGraph::NodeKind::TechnicalImage:
            node.title = TechnicalImageTitle(node.technicalImageSettings.operation);
            break;
        case EditorNodeGraph::NodeKind::Compound:
            if (node.title.empty()) node.title = "Compound";
            break;
        }
    if (node.kind != EditorNodeGraph::NodeKind::Compound) {
        ApplyLiveDefinitionIdentity(node);
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
        case EditorNodeGraph::NodeKind::RawDevelopment:
            add(EditorNodeGraph::kImageOutputSocketId, EditorNodeGraph::SocketDirection::Output, EditorNodeGraph::SocketType::Image, "Image", false, true);
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
        case EditorNodeGraph::NodeKind::Mfsr:
            add(EditorNodeGraph::kMfsrReferenceInputSocketId, EditorNodeGraph::SocketDirection::Input, EditorNodeGraph::SocketType::Image, "Reference", false, true);
            add(EditorNodeGraph::MfsrInputSocketId(1).c_str(), EditorNodeGraph::SocketDirection::Input, EditorNodeGraph::SocketType::Image, "Frame 2", true, true);
            for (int inputIndex = 2; inputIndex < EditorNodeGraph::kMaxMfsrInputCount; ++inputIndex) {
                const std::string socketId = EditorNodeGraph::MfsrInputSocketId(inputIndex);
                const std::string label = EditorNodeGraph::MfsrInputSocketLabel(inputIndex);
                add(socketId.c_str(), EditorNodeGraph::SocketDirection::Input, EditorNodeGraph::SocketType::Image, label.c_str(), true, false);
            }
            add(EditorNodeGraph::kImageOutputSocketId, EditorNodeGraph::SocketDirection::Output, EditorNodeGraph::SocketType::Image, "Image", false, true);
            break;
        case EditorNodeGraph::NodeKind::RawProjectFrame:
            add(EditorNodeGraph::kRawOutputSocketId, EditorNodeGraph::SocketDirection::Output, EditorNodeGraph::SocketType::Raw, "Mosaic RAW", false, true);
            break;
        case EditorNodeGraph::NodeKind::MultiFrameDenoise:
            for (const EditorNodeGraph::MfdFrameBinding& binding :
                 node.multiFrameDenoise.frameBindings) {
                const std::string socketId = binding.socketId.empty()
                    ? EditorNodeGraph::MfdFrameInputSocketId(binding.frameId)
                    : binding.socketId;
                const std::string label = binding.reference
                    ? "Reference - " + binding.label
                    : binding.label;
                add(
                    socketId.c_str(),
                    EditorNodeGraph::SocketDirection::Input,
                    EditorNodeGraph::SocketType::Raw,
                    label.empty() ? "RAW Frame" : label.c_str(),
                    false,
                    true);
            }
            add(EditorNodeGraph::kImageOutputSocketId, EditorNodeGraph::SocketDirection::Output, EditorNodeGraph::SocketType::Image, "Developed result", false, true);
            break;
        case EditorNodeGraph::NodeKind::MultiFrameHdr:
            if (!node.multiFrameHdr.bracketingSource) {
                for (const EditorNodeGraph::MfdFrameBinding& binding :
                     node.multiFrameHdr.frameBindings) {
                    const std::string socketId = binding.socketId.empty()
                        ? EditorNodeGraph::MfdFrameInputSocketId(binding.frameId)
                        : binding.socketId;
                    std::string label = binding.label;
                    if (binding.reference) label = "Geometric reference - " + label;
                    if (binding.frameId == node.multiFrameHdr.radiometricAnchorFrameId)
                        label = "Radiometric anchor - " + label;
                    add(socketId.c_str(), EditorNodeGraph::SocketDirection::Input,
                        EditorNodeGraph::SocketType::Raw,
                        label.empty() ? "RAW Frame" : label.c_str(), false, true);
                }
            }
            add(EditorNodeGraph::kImageOutputSocketId, EditorNodeGraph::SocketDirection::Output,
                EditorNodeGraph::SocketType::Image,
                node.multiFrameHdr.bracketingSource ? "Image" : "Scene-linear HDR", false, true);
            break;
        case EditorNodeGraph::NodeKind::RawProjectSourceSet:
            add(EditorNodeGraph::kImageOutputSocketId, EditorNodeGraph::SocketDirection::Output, EditorNodeGraph::SocketType::Image, "Result unavailable", false, true);
            break;
        case EditorNodeGraph::NodeKind::Lut:
            add(EditorNodeGraph::kImageInputSocketId, EditorNodeGraph::SocketDirection::Input, EditorNodeGraph::SocketType::Image, "Image", false, true);
            add(EditorNodeGraph::kMaskInputSocketId, EditorNodeGraph::SocketDirection::Input, EditorNodeGraph::SocketType::Mask, "Mask", true, true);
            add(EditorNodeGraph::kImageOutputSocketId, EditorNodeGraph::SocketDirection::Output, EditorNodeGraph::SocketType::Image, "Image", false, true);
            break;
        case EditorNodeGraph::NodeKind::RawOperation:
            add(EditorNodeGraph::kImageInputSocketId, EditorNodeGraph::SocketDirection::Input,
                (node.rawOperation.kind==Stack::RawRecipe::GraphOperationKind::ToneCurve || node.rawOperation.kind==Stack::RawRecipe::GraphOperationKind::Exposure) ? EditorNodeGraph::SocketType::ImageOrChannel : EditorNodeGraph::SocketType::Image,
                "Image", false, true);
            add(EditorNodeGraph::kMaskInputSocketId, EditorNodeGraph::SocketDirection::Input, EditorNodeGraph::SocketType::Mask, "Mask", true, true);
            add("inputImageOut", EditorNodeGraph::SocketDirection::Output, EditorNodeGraph::SocketType::Image, "Operation input", false, true);
            if (node.rawOperation.kind == Stack::RawRecipe::GraphOperationKind::LocalEv) {
                for (const auto& gradient : node.rawOperation.parameters.value("evGradients", nlohmann::json::array()))
                    add(("gradient:" + gradient.at("mask").value("id", std::string{})).c_str(), EditorNodeGraph::SocketDirection::Input,
                        EditorNodeGraph::SocketType::Mask, "Gradient coverage", true, true);
                for (const auto& area : node.rawOperation.parameters.value("localRange", nlohmann::json::object()).value("areas", nlohmann::json::array()))
                    add(("area:" + area.value("id", std::string{})).c_str(), EditorNodeGraph::SocketDirection::Input,
                        EditorNodeGraph::SocketType::Mask, "Area coverage", true, true);
            }
            add(EditorNodeGraph::kImageOutputSocketId, EditorNodeGraph::SocketDirection::Output, EditorNodeGraph::SocketType::Image, "Image", false, true);
            break;
        case EditorNodeGraph::NodeKind::Layer:
            add(EditorNodeGraph::kImageInputSocketId, EditorNodeGraph::SocketDirection::Input, EditorNodeGraph::SocketType::Image, "Image", false, true);
            add(EditorNodeGraph::kMaskInputSocketId, EditorNodeGraph::SocketDirection::Input, EditorNodeGraph::SocketType::Mask, "Mask", true, true);
            add(EditorNodeGraph::kImageOutputSocketId, EditorNodeGraph::SocketDirection::Output, EditorNodeGraph::SocketType::Image, "Image", false, true);
            break;
        case EditorNodeGraph::NodeKind::Output:
            add(EditorNodeGraph::kImageInputSocketId, EditorNodeGraph::SocketDirection::Input, EditorNodeGraph::SocketType::ImageOrChannel, "Result · Image or Channel", false, true);
            break;
        case EditorNodeGraph::NodeKind::Composite:
            break;
        case EditorNodeGraph::NodeKind::Scope:
            add(EditorNodeGraph::kScopeInputSocketId, EditorNodeGraph::SocketDirection::Input, EditorNodeGraph::SocketType::Analysis, "Scope", false, true);
            break;
        case EditorNodeGraph::NodeKind::Preview:
            add(EditorNodeGraph::kPreviewInputSocketId, EditorNodeGraph::SocketDirection::Input, EditorNodeGraph::SocketType::Analysis, "Image / Mask", false, true);
            break;
        case EditorNodeGraph::NodeKind::MaskTexture:
            add(EditorNodeGraph::kMaskOutputSocketId, EditorNodeGraph::SocketDirection::Output, EditorNodeGraph::SocketType::Mask, "Texture", false, true);
            break;
        case EditorNodeGraph::NodeKind::MaskGenerator:
            add(EditorNodeGraph::kMatchExtentInputSocketId, EditorNodeGraph::SocketDirection::Input, EditorNodeGraph::SocketType::Image, "Reference image", true, true);
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
        case EditorNodeGraph::NodeKind::Value: {
            const EditorNodeGraph::SocketType socketType = ValueSocketType(node.value.value.logicalType);
            sockets.push_back(EditorNodeGraph::SocketDefinition{
                EditorNodeGraph::kValueOutputSocketId,
                node.id,
                EditorNodeGraph::SocketDirection::Output,
                socketType,
                "Value",
                false,
                true });
            break;
        }
        case EditorNodeGraph::NodeKind::FieldMean:
            add(EditorNodeGraph::kReductionFieldInputSocketId, EditorNodeGraph::SocketDirection::Input, EditorNodeGraph::SocketType::ScalarField, "Field", false, true);
            add(EditorNodeGraph::kValueOutputSocketId, EditorNodeGraph::SocketDirection::Output, EditorNodeGraph::SocketType::Scalar, "Mean", false, true);
            break;
        case EditorNodeGraph::NodeKind::Reformat:
            add(EditorNodeGraph::kImageInputSocketId, EditorNodeGraph::SocketDirection::Input, EditorNodeGraph::SocketType::Image, "Image", false, true);
            add(EditorNodeGraph::kImageOutputSocketId, EditorNodeGraph::SocketDirection::Output, EditorNodeGraph::SocketType::Image, "Image", false, true);
            break;
        case EditorNodeGraph::NodeKind::TechnicalImage:
            add(EditorNodeGraph::kImageInputSocketId, EditorNodeGraph::SocketDirection::Input, EditorNodeGraph::SocketType::Image, "Image", false, true);
            if (node.technicalImageSettings.operation == Stack::NodeMath::TechnicalImageOperation::Exposure) {
                add(EditorNodeGraph::kExposureValueInputSocketId, EditorNodeGraph::SocketDirection::Input, EditorNodeGraph::SocketType::Scalar, "EV", true, true);
            }
            add(EditorNodeGraph::kImageOutputSocketId, EditorNodeGraph::SocketDirection::Output, EditorNodeGraph::SocketType::Image, "Image", false, true);
            break;
        case EditorNodeGraph::NodeKind::ChannelSplit:
            add(EditorNodeGraph::kImageInputSocketId, EditorNodeGraph::SocketDirection::Input, EditorNodeGraph::SocketType::Image, "Image", false, true);
            add("r", EditorNodeGraph::SocketDirection::Output, EditorNodeGraph::SocketType::Channel, "R", false, true);
            add("g", EditorNodeGraph::SocketDirection::Output, EditorNodeGraph::SocketType::Channel, "G", false, true);
            add("b", EditorNodeGraph::SocketDirection::Output, EditorNodeGraph::SocketType::Channel, "B", false, true);
            add("a", EditorNodeGraph::SocketDirection::Output, EditorNodeGraph::SocketType::Channel, "A", false, true);
            break;
        case EditorNodeGraph::NodeKind::ChannelCombine:
            add("r", EditorNodeGraph::SocketDirection::Input, EditorNodeGraph::SocketType::Channel, "R", true, true);
            add("g", EditorNodeGraph::SocketDirection::Input, EditorNodeGraph::SocketType::Channel, "G", true, true);
            add("b", EditorNodeGraph::SocketDirection::Input, EditorNodeGraph::SocketType::Channel, "B", true, true);
            add("a", EditorNodeGraph::SocketDirection::Input, EditorNodeGraph::SocketType::Channel, "A", true, true);
            add(EditorNodeGraph::kImageOutputSocketId, EditorNodeGraph::SocketDirection::Output, EditorNodeGraph::SocketType::Image, "Image", false, true);
            break;
        case EditorNodeGraph::NodeKind::ConstantChannel:
            add(EditorNodeGraph::kMatchExtentInputSocketId, EditorNodeGraph::SocketDirection::Input, EditorNodeGraph::SocketType::Channel, "Match Extent", false, true);
            sockets.back().visibilityTier = EditorNodeGraph::SocketVisibilityTier::Advanced;
            add(EditorNodeGraph::kChannelOutputSocketId, EditorNodeGraph::SocketDirection::Output, EditorNodeGraph::SocketType::Channel, "Channel", false, true);
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
        
        
        
        case EditorNodeGraph::NodeKind::RawOperation:
        case EditorNodeGraph::NodeKind::TechnicalImage:
        case EditorNodeGraph::NodeKind::Reformat:
            return EditorNodeGraph::kImageInputSocketId;
        case EditorNodeGraph::NodeKind::Value:
            break;
        case EditorNodeGraph::NodeKind::FieldMean:
            return EditorNodeGraph::kReductionFieldInputSocketId;
        case EditorNodeGraph::NodeKind::HdrMerge:
            return EditorNodeGraph::kHdrMergeInput1SocketId;
        case EditorNodeGraph::NodeKind::Mfsr:
            return EditorNodeGraph::kMfsrReferenceInputSocketId;
        case EditorNodeGraph::NodeKind::MultiFrameDenoise:
            return node.multiFrameDenoise.frameBindings.empty()
                ? std::string()
                : (node.multiFrameDenoise.frameBindings.front().socketId.empty()
                    ? EditorNodeGraph::MfdFrameInputSocketId(
                        node.multiFrameDenoise.frameBindings.front().frameId)
                    : node.multiFrameDenoise.frameBindings.front().socketId);
        case EditorNodeGraph::NodeKind::MultiFrameHdr:
            return node.multiFrameHdr.bracketingSource || node.multiFrameHdr.frameBindings.empty()
                ? std::string()
                : (node.multiFrameHdr.frameBindings.front().socketId.empty()
                    ? EditorNodeGraph::MfdFrameInputSocketId(
                        node.multiFrameHdr.frameBindings.front().frameId)
                    : node.multiFrameHdr.frameBindings.front().socketId);
        case EditorNodeGraph::NodeKind::RawDecode:
        case EditorNodeGraph::NodeKind::RawDevelop:
            return EditorNodeGraph::kRawInputSocketId;
        case EditorNodeGraph::NodeKind::ChannelCombine:
            return "r";
        case EditorNodeGraph::NodeKind::ConstantChannel:
            return EditorNodeGraph::kMatchExtentInputSocketId;
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
        case EditorNodeGraph::NodeKind::MaskTexture:
        case EditorNodeGraph::NodeKind::MaskGenerator:
        case EditorNodeGraph::NodeKind::CustomMask:
        case EditorNodeGraph::NodeKind::ImageGenerator:
        case EditorNodeGraph::NodeKind::RawDevelopment:
        case EditorNodeGraph::NodeKind::RawSource:
        case EditorNodeGraph::NodeKind::Image:
        
        
        case EditorNodeGraph::NodeKind::RawProjectFrame:
        case EditorNodeGraph::NodeKind::RawProjectSourceSet:
            break;
    }
    return {};
}

std::string DefaultOutputSocket(const EditorNodeGraph::Node& node) {
    switch (node.kind) {
        case EditorNodeGraph::NodeKind::Compound: {
            const auto& ports = node.compound.instance.interfaceSnapshot;
            for (const auto& port : ports)
                if (port.direction == Stack::NodeMath::PortDirection::Output && !port.advanced) return port.id;
            for (const auto& port : ports)
                if (port.direction == Stack::NodeMath::PortDirection::Output) return port.id;
            return {};
        }
        case EditorNodeGraph::NodeKind::Image:
        case EditorNodeGraph::NodeKind::RawDevelopment:
        case EditorNodeGraph::NodeKind::RawDecode:
        case EditorNodeGraph::NodeKind::RawDevelop:
        case EditorNodeGraph::NodeKind::RawDetailFusion:
        case EditorNodeGraph::NodeKind::HdrMerge:
        case EditorNodeGraph::NodeKind::Mfsr:
        case EditorNodeGraph::NodeKind::MultiFrameDenoise:
        case EditorNodeGraph::NodeKind::MultiFrameHdr:
        case EditorNodeGraph::NodeKind::RawProjectSourceSet:
        case EditorNodeGraph::NodeKind::Lut:
        case EditorNodeGraph::NodeKind::Layer:
        case EditorNodeGraph::NodeKind::Mix:
        case EditorNodeGraph::NodeKind::ImageGenerator:
        case EditorNodeGraph::NodeKind::ChannelCombine:
        case EditorNodeGraph::NodeKind::DataMath:
        
        
        case EditorNodeGraph::NodeKind::RawOperation:
        case EditorNodeGraph::NodeKind::TechnicalImage:
        case EditorNodeGraph::NodeKind::Reformat:
            return EditorNodeGraph::kImageOutputSocketId;
        
        
        case EditorNodeGraph::NodeKind::ConstantChannel:
            return EditorNodeGraph::kChannelOutputSocketId;
        case EditorNodeGraph::NodeKind::Value:
            return EditorNodeGraph::kValueOutputSocketId;
        case EditorNodeGraph::NodeKind::FieldMean:
            return EditorNodeGraph::kValueOutputSocketId;
        case EditorNodeGraph::NodeKind::RawSource:
        case EditorNodeGraph::NodeKind::RawProjectFrame:
            return EditorNodeGraph::kRawOutputSocketId;
        case EditorNodeGraph::NodeKind::ChannelSplit:
            return "r";
        case EditorNodeGraph::NodeKind::MaskTexture:
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



EditorNodeGraph::Node BuildPrototypeNode(const NodeCatalogEntry& entry) {
    auto node = BuildOperationPrototype(entry.kind, entry.value);
    ApplyNodeMetadata(node);
    return node;
}

Stack::NodeMath::FirstClassValue BuildDefaultFirstClassValue(
    Stack::NodeMath::LogicalValueType type) {
    return DefaultValue(type);
}

} // namespace EditorNodeGraphDefinitions
