#include "EditorNodeGraphDefinitions.h"

#include "Editor/LayerRegistry.h"
#include "Editor/NodeGraph/UnifiedNodeDefinitionRegistry.h"

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

const char* FrequencyMaskTitle(EditorNodeGraph::FrequencyMaskShape shape) {
    switch (shape) {
        case EditorNodeGraph::FrequencyMaskShape::LowPass: return "Low Pass";
        case EditorNodeGraph::FrequencyMaskShape::HighPass: return "High Pass";
        case EditorNodeGraph::FrequencyMaskShape::BandPass: return "Band Pass";
        case EditorNodeGraph::FrequencyMaskShape::BandStop: return "Band Stop";
        case EditorNodeGraph::FrequencyMaskShape::Notch: return "Notch";
        case EditorNodeGraph::FrequencyMaskShape::Gaussian: return "Gaussian";
        case EditorNodeGraph::FrequencyMaskShape::Butterworth: return "Butterworth";
    }
    return "Frequency Mask";
}

const char* SpectrumMathTitle(EditorNodeGraph::SpectrumMathMode mode) {
    switch (mode) {
        case EditorNodeGraph::SpectrumMathMode::Multiply: return "Filter Spectrum";
        case EditorNodeGraph::SpectrumMathMode::Add: return "Add Spectra";
        case EditorNodeGraph::SpectrumMathMode::Subtract: return "Subtract Spectra";
        case EditorNodeGraph::SpectrumMathMode::Difference: return "Spectrum Difference";
    }
    return "Spectrum Math";
}

const char* MagnitudePhaseTitle(EditorNodeGraph::MagnitudePhaseMode mode) {
    switch (mode) {
        case EditorNodeGraph::MagnitudePhaseMode::Magnitude: return "Magnitude";
        case EditorNodeGraph::MagnitudePhaseMode::Phase: return "Phase";
        case EditorNodeGraph::MagnitudePhaseMode::Recombine: return "Recombine Magnitude/Phase";
    }
    return "Magnitude / Phase";
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
        case EditorNodeGraph::NodeKind::RawDevelopment: return "raw-development";
        case EditorNodeGraph::NodeKind::RawDecode: return "raw-decode";
        case EditorNodeGraph::NodeKind::RawDevelop: return "develop";
        case EditorNodeGraph::NodeKind::RawDetailAutoMask: return "raw-detail-automask";
        case EditorNodeGraph::NodeKind::RawDetailFusion: return "raw-detail-fusion";
        case EditorNodeGraph::NodeKind::HdrMerge: return "hdr-merge";
        case EditorNodeGraph::NodeKind::Mfsr: return "mfsr";
        case EditorNodeGraph::NodeKind::RawProjectFrame: return "raw-project-frame";
        case EditorNodeGraph::NodeKind::MultiFrameDenoise: return "multi-frame-denoise";
        case EditorNodeGraph::NodeKind::RawProjectSourceSet: return "raw-project-source-set";
        case EditorNodeGraph::NodeKind::Lut: return "lut";
        case EditorNodeGraph::NodeKind::CustomMask: return "custom-mask";
        case EditorNodeGraph::NodeKind::Mix: return "blend-images";
        case EditorNodeGraph::NodeKind::ChannelSplit: return "channel-split";
        case EditorNodeGraph::NodeKind::ChannelCombine: return "channel-combine";
        case EditorNodeGraph::NodeKind::ConstantChannel: return "constant-channel";
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
                case EditorNodeGraph::DataMathMode::ImageAverage: return "data-math:image-average";
            }
            return "data-math";
        case EditorNodeGraph::NodeKind::Value:
            return std::string("value:") +
                Stack::NodeMath::SerializeFirstClassValue(DefaultValue(
                    static_cast<Stack::NodeMath::LogicalValueType>(value))).value("logicalType", "invalid");
        case EditorNodeGraph::NodeKind::FieldMean: return "analysis:field-mean";
        case EditorNodeGraph::NodeKind::Reformat: return "geometry:reformat";
        case EditorNodeGraph::NodeKind::TechnicalImage:
            return std::string("technical-image:") +
                Stack::NodeMath::TechnicalOperationIdentity(
                    static_cast<Stack::NodeMath::TechnicalImageOperation>(value));
        case EditorNodeGraph::NodeKind::FrequencyFilter:
            return "frequency:filter:" + std::to_string(value);
        case EditorNodeGraph::NodeKind::FrequencyResponse:
            return "frequency:response";
        case EditorNodeGraph::NodeKind::FrequencyFft: return "frequency:fft";
        case EditorNodeGraph::NodeKind::FrequencyIfft: return "frequency:ifft";
        case EditorNodeGraph::NodeKind::SpectrumView: return "frequency:spectrum-view";
        case EditorNodeGraph::NodeKind::ApplyFrequencyResponse: return "frequency:apply-response";
        case EditorNodeGraph::NodeKind::CombineSpectra: return "frequency:combine-spectra";
        case EditorNodeGraph::NodeKind::SpectrumSeparate: return "frequency:separate-spectrum";
        case EditorNodeGraph::NodeKind::SpectrumRecombine: return "frequency:recombine-spectrum";
        case EditorNodeGraph::NodeKind::FrequencyMask:
            switch (static_cast<EditorNodeGraph::FrequencyMaskShape>(value)) {
                case EditorNodeGraph::FrequencyMaskShape::LowPass: return "frequency-mask:low-pass";
                case EditorNodeGraph::FrequencyMaskShape::HighPass: return "frequency-mask:high-pass";
                case EditorNodeGraph::FrequencyMaskShape::BandPass: return "frequency-mask:band-pass";
                case EditorNodeGraph::FrequencyMaskShape::BandStop: return "frequency-mask:band-stop";
                case EditorNodeGraph::FrequencyMaskShape::Notch: return "frequency-mask:notch";
                case EditorNodeGraph::FrequencyMaskShape::Gaussian: return "frequency-mask:gaussian";
                case EditorNodeGraph::FrequencyMaskShape::Butterworth: return "frequency-mask:butterworth";
            }
            return "frequency-mask";
        case EditorNodeGraph::NodeKind::SpectrumMath:
            switch (static_cast<EditorNodeGraph::SpectrumMathMode>(value)) {
                case EditorNodeGraph::SpectrumMathMode::Multiply: return "spectrum-math:multiply";
                case EditorNodeGraph::SpectrumMathMode::Add: return "spectrum-math:add";
                case EditorNodeGraph::SpectrumMathMode::Subtract: return "spectrum-math:subtract";
                case EditorNodeGraph::SpectrumMathMode::Difference: return "spectrum-math:difference";
            }
            return "spectrum-math";
        case EditorNodeGraph::NodeKind::MagnitudePhase:
            switch (static_cast<EditorNodeGraph::MagnitudePhaseMode>(value)) {
                case EditorNodeGraph::MagnitudePhaseMode::Magnitude: return "magnitude-phase:magnitude";
                case EditorNodeGraph::MagnitudePhaseMode::Phase: return "magnitude-phase:phase";
                case EditorNodeGraph::MagnitudePhaseMode::Recombine: return "magnitude-phase:recombine";
            }
            return "magnitude-phase";
        case EditorNodeGraph::NodeKind::SpectrumAnalyzer: return "spectrum-analyzer";
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
        case EditorNodeGraph::NodeKind::RawProjectSourceSet:
            node.title = "RAW Project Source Set";
            node.expanded = false;
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
            node.title = "Reformat";
            node.expanded = true;
            break;
        case EditorNodeGraph::NodeKind::TechnicalImage:
            node.title = TechnicalImageTitle(node.technicalImageSettings.operation);
            break;
        case EditorNodeGraph::NodeKind::Compound:
            if (node.title.empty()) node.title = "Compound";
            break;
        case EditorNodeGraph::NodeKind::FrequencyFilter:
            node.title = "Frequency Filter";
            break;
        case EditorNodeGraph::NodeKind::FrequencyResponse:
            node.title = "Frequency Response";
            break;
        case EditorNodeGraph::NodeKind::FrequencyFft:
            node.title = "Fourier Transform";
            break;
        case EditorNodeGraph::NodeKind::FrequencyIfft:
            node.title = "Inverse Fourier Transform";
            break;
        case EditorNodeGraph::NodeKind::SpectrumView:
            node.title = "Spectrum View";
            break;
        case EditorNodeGraph::NodeKind::ApplyFrequencyResponse:
            node.title = "Apply Frequency Response";
            break;
        case EditorNodeGraph::NodeKind::CombineSpectra:
            node.title = "Combine Spectra";
            break;
        case EditorNodeGraph::NodeKind::SpectrumSeparate:
            node.title = "Separate Spectrum";
            break;
        case EditorNodeGraph::NodeKind::SpectrumRecombine:
            node.title = "Recombine Spectrum";
            break;
        case EditorNodeGraph::NodeKind::FrequencyMask:
            node.title = FrequencyMaskTitle(node.frequencyMaskShape);
            node.frequencyMaskSettings.shape = node.frequencyMaskShape;
            break;
        case EditorNodeGraph::NodeKind::SpectrumMath:
            node.title = SpectrumMathTitle(node.spectrumMathMode);
            break;
        case EditorNodeGraph::NodeKind::MagnitudePhase:
            node.title = MagnitudePhaseTitle(node.magnitudePhaseMode);
            break;
        case EditorNodeGraph::NodeKind::SpectrumAnalyzer:
            node.title = "Spectrum Analyzer";
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
        case EditorNodeGraph::NodeKind::RawProjectSourceSet:
            add(EditorNodeGraph::kImageOutputSocketId, EditorNodeGraph::SocketDirection::Output, EditorNodeGraph::SocketType::Image, "Result unavailable", false, true);
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
        case EditorNodeGraph::NodeKind::FrequencyFilter:
            add(EditorNodeGraph::kChannelInputSocketId, EditorNodeGraph::SocketDirection::Input, EditorNodeGraph::SocketType::Channel, "Channel", false, true);
            add(EditorNodeGraph::kFrequencyResponseInputSocketId, EditorNodeGraph::SocketDirection::Input, EditorNodeGraph::SocketType::FrequencyResponse, "Response", true, true);
            add(EditorNodeGraph::kChannelOutputSocketId, EditorNodeGraph::SocketDirection::Output, EditorNodeGraph::SocketType::Channel, "Filtered", false, true);
            break;
        case EditorNodeGraph::NodeKind::FrequencyResponse:
            add(EditorNodeGraph::kFrequencyResponseOutputSocketId, EditorNodeGraph::SocketDirection::Output, EditorNodeGraph::SocketType::FrequencyResponse, "Response", false, true);
            break;
        case EditorNodeGraph::NodeKind::FrequencyFft:
            add(EditorNodeGraph::kChannelInputSocketId, EditorNodeGraph::SocketDirection::Input, EditorNodeGraph::SocketType::Channel, "Channel", false, true);
            add(EditorNodeGraph::kSpectrumOutputSocketId, EditorNodeGraph::SocketDirection::Output, EditorNodeGraph::SocketType::Spectrum, "Spectrum", false, true);
            break;
        case EditorNodeGraph::NodeKind::FrequencyIfft:
            add(EditorNodeGraph::kSpectrumInputSocketId, EditorNodeGraph::SocketDirection::Input, EditorNodeGraph::SocketType::Spectrum, "Spectrum", false, true);
            add(EditorNodeGraph::kChannelOutputSocketId, EditorNodeGraph::SocketDirection::Output, EditorNodeGraph::SocketType::Channel, "Channel", false, true);
            break;
        case EditorNodeGraph::NodeKind::SpectrumView:
            add(EditorNodeGraph::kSpectrumInputSocketId, EditorNodeGraph::SocketDirection::Input, EditorNodeGraph::SocketType::Spectrum, "Spectrum", false, true);
            add(EditorNodeGraph::kImageOutputSocketId, EditorNodeGraph::SocketDirection::Output, EditorNodeGraph::SocketType::Image, "View", false, true);
            break;
        case EditorNodeGraph::NodeKind::ApplyFrequencyResponse:
            add(EditorNodeGraph::kSpectrumInputSocketId, EditorNodeGraph::SocketDirection::Input, EditorNodeGraph::SocketType::Spectrum, "Spectrum", false, true);
            add(EditorNodeGraph::kFrequencyResponseInputSocketId, EditorNodeGraph::SocketDirection::Input, EditorNodeGraph::SocketType::FrequencyResponse, "Response", false, true);
            add(EditorNodeGraph::kSpectrumOutputSocketId, EditorNodeGraph::SocketDirection::Output, EditorNodeGraph::SocketType::Spectrum, "Spectrum", false, true);
            break;
        case EditorNodeGraph::NodeKind::CombineSpectra:
            add(EditorNodeGraph::kSpectrumInputASocketId, EditorNodeGraph::SocketDirection::Input, EditorNodeGraph::SocketType::Spectrum, "Spectrum A", false, true);
            add(EditorNodeGraph::kSpectrumInputBSocketId, EditorNodeGraph::SocketDirection::Input, EditorNodeGraph::SocketType::Spectrum, "Spectrum B", false, true);
            add(EditorNodeGraph::kSpectrumOutputSocketId, EditorNodeGraph::SocketDirection::Output, EditorNodeGraph::SocketType::Spectrum, "Spectrum", false, true);
            break;
        case EditorNodeGraph::NodeKind::SpectrumSeparate:
            add(EditorNodeGraph::kSpectrumInputSocketId, EditorNodeGraph::SocketDirection::Input, EditorNodeGraph::SocketType::Spectrum, "Spectrum", false, true);
            add(EditorNodeGraph::kSpectrumMagnitudeOutputSocketId, EditorNodeGraph::SocketDirection::Output, EditorNodeGraph::SocketType::SpectrumMagnitude, "Magnitude", false, true);
            add(EditorNodeGraph::kSpectrumPhaseOutputSocketId, EditorNodeGraph::SocketDirection::Output, EditorNodeGraph::SocketType::SpectrumPhase, "Phase", false, true);
            break;
        case EditorNodeGraph::NodeKind::SpectrumRecombine:
            add(EditorNodeGraph::kSpectrumMagnitudeInputSocketId, EditorNodeGraph::SocketDirection::Input, EditorNodeGraph::SocketType::SpectrumMagnitude, "Magnitude", false, true);
            add(EditorNodeGraph::kSpectrumPhaseInputSocketId, EditorNodeGraph::SocketDirection::Input, EditorNodeGraph::SocketType::SpectrumPhase, "Phase", false, true);
            add(EditorNodeGraph::kSpectrumOutputSocketId, EditorNodeGraph::SocketDirection::Output, EditorNodeGraph::SocketType::Spectrum, "Spectrum", false, true);
            break;
        case EditorNodeGraph::NodeKind::FrequencyMask:
            add(EditorNodeGraph::kMaskOutputSocketId, EditorNodeGraph::SocketDirection::Output, EditorNodeGraph::SocketType::Mask, "Filter", false, true);
            break;
        case EditorNodeGraph::NodeKind::SpectrumMath:
            add(EditorNodeGraph::kMixInputASocketId, EditorNodeGraph::SocketDirection::Input, EditorNodeGraph::SocketType::Image, "Spectrum A", false, true);
            add(EditorNodeGraph::kMixInputBSocketId, EditorNodeGraph::SocketDirection::Input, EditorNodeGraph::SocketType::Image, "Spectrum B", true, true);
            add(EditorNodeGraph::kMaskInputSocketId, EditorNodeGraph::SocketDirection::Input, EditorNodeGraph::SocketType::Mask, "Filter", true, true);
            add(EditorNodeGraph::kImageOutputSocketId, EditorNodeGraph::SocketDirection::Output, EditorNodeGraph::SocketType::Image, "Spectrum", false, true);
            break;
        case EditorNodeGraph::NodeKind::MagnitudePhase:
            add(EditorNodeGraph::kImageInputSocketId, EditorNodeGraph::SocketDirection::Input, EditorNodeGraph::SocketType::Image, "Spectrum", true, true);
            add("magnitude", EditorNodeGraph::SocketDirection::Input, EditorNodeGraph::SocketType::Mask, "Magnitude", true, false);
            add("phase", EditorNodeGraph::SocketDirection::Input, EditorNodeGraph::SocketType::Mask, "Phase", true, false);
            add(EditorNodeGraph::kMaskOutputSocketId, EditorNodeGraph::SocketDirection::Output, EditorNodeGraph::SocketType::Mask, "Component", false, true);
            add(EditorNodeGraph::kImageOutputSocketId, EditorNodeGraph::SocketDirection::Output, EditorNodeGraph::SocketType::Image, "Spectrum", false, true);
            break;
        case EditorNodeGraph::NodeKind::SpectrumAnalyzer:
            add(EditorNodeGraph::kSpectrumInputSocketId, EditorNodeGraph::SocketDirection::Input, EditorNodeGraph::SocketType::Spectrum, "Spectrum", false, true);
            add(EditorNodeGraph::kRadialPowerOutputSocketId, EditorNodeGraph::SocketDirection::Output, EditorNodeGraph::SocketType::Analysis, "Radial Power", false, true);
            add(EditorNodeGraph::kBandPowerOutputSocketId, EditorNodeGraph::SocketDirection::Output, EditorNodeGraph::SocketType::Scalar, "Band Power", false, true);
            add(EditorNodeGraph::kPeakFrequencyOutputSocketId, EditorNodeGraph::SocketDirection::Output, EditorNodeGraph::SocketType::Scalar, "Peak Frequency", false, true);
            add(EditorNodeGraph::kPeakDirectionOutputSocketId, EditorNodeGraph::SocketDirection::Output, EditorNodeGraph::SocketType::Scalar, "Peak Direction", false, true);
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
        case EditorNodeGraph::NodeKind::FrequencyFilter:
        case EditorNodeGraph::NodeKind::FrequencyFft:
        case EditorNodeGraph::NodeKind::MagnitudePhase:
        case EditorNodeGraph::NodeKind::TechnicalImage:
        case EditorNodeGraph::NodeKind::Reformat:
            return node.kind == EditorNodeGraph::NodeKind::FrequencyFilter ||
                   node.kind == EditorNodeGraph::NodeKind::FrequencyFft
                ? EditorNodeGraph::kChannelInputSocketId
                : EditorNodeGraph::kImageInputSocketId;
        case EditorNodeGraph::NodeKind::FrequencyIfft:
        case EditorNodeGraph::NodeKind::SpectrumView:
        case EditorNodeGraph::NodeKind::ApplyFrequencyResponse:
        case EditorNodeGraph::NodeKind::SpectrumSeparate:
        case EditorNodeGraph::NodeKind::SpectrumAnalyzer:
            return EditorNodeGraph::kSpectrumInputSocketId;
        case EditorNodeGraph::NodeKind::CombineSpectra:
            return EditorNodeGraph::kSpectrumInputASocketId;
        case EditorNodeGraph::NodeKind::SpectrumRecombine:
            return EditorNodeGraph::kSpectrumMagnitudeInputSocketId;
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
        case EditorNodeGraph::NodeKind::RawDecode:
        case EditorNodeGraph::NodeKind::RawDevelop:
        case EditorNodeGraph::NodeKind::RawNeuralDenoise:
            return EditorNodeGraph::kRawInputSocketId;
        case EditorNodeGraph::NodeKind::ChannelCombine:
            return "r";
        case EditorNodeGraph::NodeKind::ConstantChannel:
            return EditorNodeGraph::kMatchExtentInputSocketId;
        case EditorNodeGraph::NodeKind::Composite:
            break;
        case EditorNodeGraph::NodeKind::Mix:
        case EditorNodeGraph::NodeKind::DataMath:
        case EditorNodeGraph::NodeKind::SpectrumMath:
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
        case EditorNodeGraph::NodeKind::RawDevelopment:
        case EditorNodeGraph::NodeKind::RawSource:
        case EditorNodeGraph::NodeKind::Image:
        case EditorNodeGraph::NodeKind::FrequencyResponse:
        case EditorNodeGraph::NodeKind::FrequencyMask:
        case EditorNodeGraph::NodeKind::RawProjectFrame:
        case EditorNodeGraph::NodeKind::RawProjectSourceSet:
            break;
    }
    return {};
}

std::string DefaultOutputSocket(const EditorNodeGraph::Node& node) {
    switch (node.kind) {
        case EditorNodeGraph::NodeKind::Image:
        case EditorNodeGraph::NodeKind::RawDevelopment:
        case EditorNodeGraph::NodeKind::RawDecode:
        case EditorNodeGraph::NodeKind::RawDevelop:
        case EditorNodeGraph::NodeKind::RawDetailFusion:
        case EditorNodeGraph::NodeKind::HdrMerge:
        case EditorNodeGraph::NodeKind::Mfsr:
        case EditorNodeGraph::NodeKind::MultiFrameDenoise:
        case EditorNodeGraph::NodeKind::RawProjectSourceSet:
        case EditorNodeGraph::NodeKind::Lut:
        case EditorNodeGraph::NodeKind::Layer:
        case EditorNodeGraph::NodeKind::Mix:
        case EditorNodeGraph::NodeKind::ImageGenerator:
        case EditorNodeGraph::NodeKind::ChannelCombine:
        case EditorNodeGraph::NodeKind::DataMath:
        case EditorNodeGraph::NodeKind::SpectrumView:
        case EditorNodeGraph::NodeKind::SpectrumMath:
        case EditorNodeGraph::NodeKind::TechnicalImage:
        case EditorNodeGraph::NodeKind::Reformat:
            return EditorNodeGraph::kImageOutputSocketId;
        case EditorNodeGraph::NodeKind::FrequencyFilter:
        case EditorNodeGraph::NodeKind::FrequencyIfft:
        case EditorNodeGraph::NodeKind::ConstantChannel:
            return EditorNodeGraph::kChannelOutputSocketId;
        case EditorNodeGraph::NodeKind::FrequencyFft:
        case EditorNodeGraph::NodeKind::ApplyFrequencyResponse:
        case EditorNodeGraph::NodeKind::CombineSpectra:
        case EditorNodeGraph::NodeKind::SpectrumRecombine:
            return EditorNodeGraph::kSpectrumOutputSocketId;
        case EditorNodeGraph::NodeKind::FrequencyResponse:
            return EditorNodeGraph::kFrequencyResponseOutputSocketId;
        case EditorNodeGraph::NodeKind::SpectrumSeparate:
            return EditorNodeGraph::kSpectrumMagnitudeOutputSocketId;
        case EditorNodeGraph::NodeKind::Value:
            return EditorNodeGraph::kValueOutputSocketId;
        case EditorNodeGraph::NodeKind::FieldMean:
            return EditorNodeGraph::kValueOutputSocketId;
        case EditorNodeGraph::NodeKind::RawSource:
        case EditorNodeGraph::NodeKind::RawProjectFrame:
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
        case EditorNodeGraph::NodeKind::FrequencyMask:
        case EditorNodeGraph::NodeKind::MagnitudePhase:
            return EditorNodeGraph::kMaskOutputSocketId;
        case EditorNodeGraph::NodeKind::Composite:
        case EditorNodeGraph::NodeKind::Output:
        case EditorNodeGraph::NodeKind::Scope:
        case EditorNodeGraph::NodeKind::Preview:
        case EditorNodeGraph::NodeKind::SpectrumAnalyzer:
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
        EditorNodeGraph::NodeKind::RawDevelopment,
        0,
        "RAW Development",
        "Input / Output",
        NodeCatalogPreviewStrategy::NoPreview,
        1));
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
        "Develop (Advanced Auto)",
        "Advanced RAW",
        NodeCatalogPreviewStrategy::NoPreview,
        2));
    entries.push_back(MakeCatalogEntry(EditorNodeGraph::NodeKind::HdrMerge, 0, "HDR Merge", "Input / Output", NodeCatalogPreviewStrategy::FallbackOnly));
    entries.push_back(MakeCatalogEntry(EditorNodeGraph::NodeKind::Mfsr, 0, "MFSR", "Input / Output", NodeCatalogPreviewStrategy::FallbackOnly));
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
    entries.push_back(MakeCatalogEntry(EditorNodeGraph::NodeKind::DataMath, static_cast<int>(EditorNodeGraph::DataMathMode::ImageAverage), "Average Images", "Image Operations"));
    using LogicalValue = Stack::NodeMath::LogicalValueType;
    entries.push_back(MakeCatalogEntry(EditorNodeGraph::NodeKind::Value, static_cast<int>(LogicalValue::Boolean), "Boolean Value", "Values", NodeCatalogPreviewStrategy::FallbackOnly));
    entries.push_back(MakeCatalogEntry(EditorNodeGraph::NodeKind::Value, static_cast<int>(LogicalValue::Integer), "Integer Value", "Values", NodeCatalogPreviewStrategy::FallbackOnly));
    entries.push_back(MakeCatalogEntry(EditorNodeGraph::NodeKind::Value, static_cast<int>(LogicalValue::Scalar), "Scalar Value", "Values", NodeCatalogPreviewStrategy::FallbackOnly));
    entries.push_back(MakeCatalogEntry(EditorNodeGraph::NodeKind::Value, static_cast<int>(LogicalValue::Vector2), "Vector 2 Value", "Values", NodeCatalogPreviewStrategy::FallbackOnly));
    entries.push_back(MakeCatalogEntry(EditorNodeGraph::NodeKind::Value, static_cast<int>(LogicalValue::Vector3), "Vector 3 Value", "Values", NodeCatalogPreviewStrategy::FallbackOnly));
    entries.push_back(MakeCatalogEntry(EditorNodeGraph::NodeKind::Value, static_cast<int>(LogicalValue::Vector4), "Vector 4 Value", "Values", NodeCatalogPreviewStrategy::FallbackOnly));
    entries.push_back(MakeCatalogEntry(EditorNodeGraph::NodeKind::Value, static_cast<int>(LogicalValue::Matrix3), "Matrix 3x3 Value", "Values", NodeCatalogPreviewStrategy::FallbackOnly));
    entries.push_back(MakeCatalogEntry(EditorNodeGraph::NodeKind::Value, static_cast<int>(LogicalValue::Matrix4), "Matrix 4x4 Value", "Values", NodeCatalogPreviewStrategy::FallbackOnly));
    entries.push_back(MakeCatalogEntry(EditorNodeGraph::NodeKind::Value, static_cast<int>(LogicalValue::Coordinate2), "Coordinate Value", "Values", NodeCatalogPreviewStrategy::FallbackOnly));
    entries.push_back(MakeCatalogEntry(EditorNodeGraph::NodeKind::Value, static_cast<int>(LogicalValue::Curve1D), "Curve Value", "Values", NodeCatalogPreviewStrategy::FallbackOnly));
    entries.push_back(MakeCatalogEntry(EditorNodeGraph::NodeKind::FieldMean, 0, "Field Mean", "Analysis / Measure", NodeCatalogPreviewStrategy::FallbackOnly));
    entries.push_back(MakeCatalogEntry(EditorNodeGraph::NodeKind::Reformat, 0, "Reformat", "Geometry / Transform", NodeCatalogPreviewStrategy::FallbackOnly));
    using TechnicalOperation = Stack::NodeMath::TechnicalImageOperation;
    entries.push_back(MakeCatalogEntry(EditorNodeGraph::NodeKind::TechnicalImage, static_cast<int>(TechnicalOperation::AssignSrgb), "Assign sRGB", "Image Technical"));
    entries.push_back(MakeCatalogEntry(EditorNodeGraph::NodeKind::TechnicalImage, static_cast<int>(TechnicalOperation::AssignLinearSrgb), "Assign Linear sRGB", "Image Technical"));
    entries.push_back(MakeCatalogEntry(EditorNodeGraph::NodeKind::TechnicalImage, static_cast<int>(TechnicalOperation::AssignLinearDisplayP3), "Assign Linear Display-P3", "Image Technical"));
    entries.push_back(MakeCatalogEntry(EditorNodeGraph::NodeKind::TechnicalImage, static_cast<int>(TechnicalOperation::SrgbDecode), "sRGB Decode", "Image Technical"));
    entries.push_back(MakeCatalogEntry(EditorNodeGraph::NodeKind::TechnicalImage, static_cast<int>(TechnicalOperation::SrgbEncode), "sRGB Encode", "Image Technical"));
    entries.push_back(MakeCatalogEntry(EditorNodeGraph::NodeKind::TechnicalImage, static_cast<int>(TechnicalOperation::LinearSrgbToDisplayP3), "Linear sRGB to Display-P3", "Image Technical"));
    entries.push_back(MakeCatalogEntry(EditorNodeGraph::NodeKind::TechnicalImage, static_cast<int>(TechnicalOperation::LinearDisplayP3ToSrgb), "Linear Display-P3 to sRGB", "Image Technical"));
    entries.push_back(MakeCatalogEntry(EditorNodeGraph::NodeKind::TechnicalImage, static_cast<int>(TechnicalOperation::Exposure), "Exposure (EV)", "Image Technical"));
    entries.push_back(MakeCatalogEntry(EditorNodeGraph::NodeKind::TechnicalImage, static_cast<int>(TechnicalOperation::Premultiply), "Premultiply", "Image Technical"));
    entries.push_back(MakeCatalogEntry(EditorNodeGraph::NodeKind::TechnicalImage, static_cast<int>(TechnicalOperation::Unpremultiply), "Unpremultiply", "Image Technical"));
    entries.push_back(MakeCatalogEntry(EditorNodeGraph::NodeKind::FrequencyFilter, static_cast<int>(EditorNodeGraph::FrequencyFilterMode::AllPass), "Frequency Filter", "Frequency"));
    entries.back().searchAliases = "FFT IFFT Fourier";
    entries.push_back(MakeCatalogEntry(EditorNodeGraph::NodeKind::FrequencyFilter, static_cast<int>(EditorNodeGraph::FrequencyFilterMode::LowPass), "Low Pass", "Frequency"));
    entries.push_back(MakeCatalogEntry(EditorNodeGraph::NodeKind::FrequencyFilter, static_cast<int>(EditorNodeGraph::FrequencyFilterMode::HighPass), "High Pass", "Frequency"));
    entries.push_back(MakeCatalogEntry(EditorNodeGraph::NodeKind::FrequencyFilter, static_cast<int>(EditorNodeGraph::FrequencyFilterMode::BandPass), "Isolate Band", "Frequency"));
    entries.push_back(MakeCatalogEntry(EditorNodeGraph::NodeKind::FrequencyFilter, static_cast<int>(EditorNodeGraph::FrequencyFilterMode::BandStop), "Suppress Band", "Frequency"));
    entries.push_back(MakeCatalogEntry(EditorNodeGraph::NodeKind::FrequencyFilter, static_cast<int>(EditorNodeGraph::FrequencyFilterMode::NotchReject), "Remove Periodic Pattern", "Frequency"));
    entries.push_back(MakeCatalogEntry(EditorNodeGraph::NodeKind::FrequencyResponse, 0, "Frequency Response", "Advanced Frequency"));
    entries.back().searchAliases = "Gaussian Butterworth response heatmap";
    entries.push_back(MakeCatalogEntry(EditorNodeGraph::NodeKind::FrequencyFft, 0, "Fourier Transform", "Advanced Frequency"));
    entries.back().searchAliases = "FFT";
    entries.push_back(MakeCatalogEntry(EditorNodeGraph::NodeKind::FrequencyIfft, 0, "Inverse Fourier Transform", "Advanced Frequency"));
    entries.back().searchAliases = "IFFT inverse FFT";
    entries.push_back(MakeCatalogEntry(EditorNodeGraph::NodeKind::SpectrumView, 0, "Spectrum View", "Advanced Frequency"));
    entries.push_back(MakeCatalogEntry(EditorNodeGraph::NodeKind::ApplyFrequencyResponse, 0, "Apply Frequency Response", "Advanced Frequency"));
    entries.push_back(MakeCatalogEntry(EditorNodeGraph::NodeKind::CombineSpectra, 0, "Combine Spectra", "Advanced Frequency"));
    entries.push_back(MakeCatalogEntry(EditorNodeGraph::NodeKind::SpectrumSeparate, 0, "Separate Spectrum", "Advanced Frequency"));
    entries.push_back(MakeCatalogEntry(EditorNodeGraph::NodeKind::SpectrumRecombine, 0, "Recombine Spectrum", "Advanced Frequency"));
    entries.push_back(MakeCatalogEntry(EditorNodeGraph::NodeKind::SpectrumAnalyzer, 0, "Spectrum Analyzer", "Advanced Frequency", NodeCatalogPreviewStrategy::FallbackOnly));
    entries.push_back(MakeCatalogEntry(EditorNodeGraph::NodeKind::ImageGenerator, static_cast<int>(EditorNodeGraph::ImageGeneratorKind::SolidColor), "Solid Color Image", "Texture / Generate"));
    entries.push_back(MakeCatalogEntry(EditorNodeGraph::NodeKind::ImageGenerator, static_cast<int>(EditorNodeGraph::ImageGeneratorKind::ColorGradient), "Color Gradient Image", "Texture / Generate"));
    entries.push_back(MakeCatalogEntry(EditorNodeGraph::NodeKind::ImageGenerator, static_cast<int>(EditorNodeGraph::ImageGeneratorKind::Square), "Square", "Texture / Generate"));
    entries.push_back(MakeCatalogEntry(EditorNodeGraph::NodeKind::ImageGenerator, static_cast<int>(EditorNodeGraph::ImageGeneratorKind::Circle), "Circle", "Texture / Generate"));
    entries.push_back(MakeCatalogEntry(EditorNodeGraph::NodeKind::ImageGenerator, static_cast<int>(EditorNodeGraph::ImageGeneratorKind::Text), "Text", "Texture / Generate"));
    entries.push_back(MakeCatalogEntry(EditorNodeGraph::NodeKind::Mix, 0, "Blend Images", "Image Operations"));
    entries.push_back(MakeCatalogEntry(EditorNodeGraph::NodeKind::Lut, 0, "LUT", "Image Operations"));
    entries.push_back(MakeCatalogEntry(EditorNodeGraph::NodeKind::ChannelSplit, 0, "Channel Split", "Channels"));
    entries.push_back(MakeCatalogEntry(EditorNodeGraph::NodeKind::ChannelCombine, 0, "Image Combine", "Channels"));
    entries.back().searchAliases = "Channel Combine RGBA";
    entries.push_back(MakeCatalogEntry(
        EditorNodeGraph::NodeKind::ConstantChannel,
        0,
        "Constant Channel",
        "Channels",
        NodeCatalogPreviewStrategy::FallbackOnly));
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
        case EditorNodeGraph::NodeKind::Value:
            node.value.value = DefaultValue(static_cast<Stack::NodeMath::LogicalValueType>(entry.value));
            break;
        case EditorNodeGraph::NodeKind::TechnicalImage:
            node.technicalImageSettings.operation =
                static_cast<Stack::NodeMath::TechnicalImageOperation>(entry.value);
            break;
        case EditorNodeGraph::NodeKind::FrequencyFilter:
            node.frequencyFilterSettings.localResponse.mode =
                static_cast<EditorNodeGraph::FrequencyFilterMode>(entry.value);
            if (node.frequencyFilterSettings.localResponse.mode ==
                EditorNodeGraph::FrequencyFilterMode::NotchReject) {
                node.frequencyFilterSettings.localResponse.notches.push_back({
                    Stack::NodeMath::GenerateCanonicalUuid(), 0.25f, 0.0f, 0.025f
                });
            }
            break;
        case EditorNodeGraph::NodeKind::FrequencyMask:
            node.frequencyMaskShape = static_cast<EditorNodeGraph::FrequencyMaskShape>(entry.value);
            node.frequencyMaskSettings.shape = node.frequencyMaskShape;
            break;
        case EditorNodeGraph::NodeKind::SpectrumMath:
            node.spectrumMathMode = static_cast<EditorNodeGraph::SpectrumMathMode>(entry.value);
            break;
        case EditorNodeGraph::NodeKind::MagnitudePhase:
            node.magnitudePhaseMode = static_cast<EditorNodeGraph::MagnitudePhaseMode>(entry.value);
            break;
        case EditorNodeGraph::NodeKind::SpectrumAnalyzer:
            node.spectrumAnalyzerMode = static_cast<EditorNodeGraph::SpectrumAnalyzerMode>(entry.value);
            break;
        default:
            break;
    }
    ApplyNodeMetadata(node);
    return node;
}

Stack::NodeMath::FirstClassValue BuildDefaultFirstClassValue(
    Stack::NodeMath::LogicalValueType type) {
    return DefaultValue(type);
}

} // namespace EditorNodeGraphDefinitions
