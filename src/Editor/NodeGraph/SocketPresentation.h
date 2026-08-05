#pragma once

#include "Editor/NodeGraph/NodeGraphTypes.h"

#include <algorithm>
#include <cctype>
#include <string>

namespace EditorNodeGraph::SocketPresentation {

inline Stack::NodeMath::LogicalValueType LogicalTypeForSocketType(SocketType type) {
    using Logical = Stack::NodeMath::LogicalValueType;
    switch (type) {
        case SocketType::ImageOrChannel: return Logical::Invalid;
        case SocketType::Mask: return Logical::Mask;
        case SocketType::Channel: return Logical::Channel;
        case SocketType::Spectrum: return Logical::ComplexSpectrum;
        case SocketType::FrequencyResponse: return Logical::FrequencyResponse;
        case SocketType::SpectrumMagnitude: return Logical::SpectrumMagnitude;
        case SocketType::SpectrumPhase: return Logical::SpectrumPhase;
        case SocketType::ScalarField: return Logical::ScalarField;
        case SocketType::Boolean: return Logical::Boolean;
        case SocketType::Integer: return Logical::Integer;
        case SocketType::Scalar: return Logical::Scalar;
        case SocketType::Vector2: return Logical::Vector2;
        case SocketType::Vector3: return Logical::Vector3;
        case SocketType::Vector4: return Logical::Vector4;
        case SocketType::Matrix3: return Logical::Matrix3;
        case SocketType::Matrix4: return Logical::Matrix4;
        case SocketType::Curve: return Logical::Curve1D;
        case SocketType::Coordinate: return Logical::Coordinate2;
        case SocketType::Histogram: return Logical::Histogram;
        case SocketType::Statistics: return Logical::Statistics;
        case SocketType::Metadata: return Logical::Metadata;
        case SocketType::Handle: return Logical::SpecializedHandle;
        case SocketType::Analysis: return Logical::Analysis;
        case SocketType::Raw: return Logical::Raw;
        case SocketType::Value: return Logical::Scalar;
        case SocketType::Image: return Logical::ColorImage;
    }
    return Logical::Invalid;
}

inline std::string RoleKeyFromSocket(const SocketDefinition& socket) {
    if (socket.type == SocketType::ImageOrChannel) return "result";
    if (socket.id == kImageInputSocketId || socket.id == kImageOutputSocketId) return "image";
    if (socket.id == kRawInputSocketId || socket.id == kRawOutputSocketId) return "raw-image-data";
    if (socket.id == kMaskInputSocketId || socket.id == kMaskOutputSocketId) return "mask";
    if (socket.id == kChannelInputSocketId || socket.id == kChannelOutputSocketId) return "channel";
    if (socket.id == kSpectrumInputSocketId || socket.id == kSpectrumOutputSocketId ||
        socket.id == kSpectrumInputASocketId || socket.id == kSpectrumInputBSocketId) return "complex-spectrum";
    if (socket.id == kFrequencyResponseInputSocketId || socket.id == kFrequencyResponseOutputSocketId) return "frequency-response";
    if (socket.id == kSpectrumMagnitudeInputSocketId || socket.id == kSpectrumMagnitudeOutputSocketId) return "magnitude";
    if (socket.id == kSpectrumPhaseInputSocketId || socket.id == kSpectrumPhaseOutputSocketId) return "phase";
    if (socket.id == kExposureValueInputSocketId) return "exposure";
    if (socket.id == kReductionFieldInputSocketId) return "field";
    if (socket.id == kMixFactorSocketId) return "factor";
    if (socket.id == "r") return "red-channel";
    if (socket.id == "g") return "green-channel";
    if (socket.id == "b") return "blue-channel";
    if (socket.id == "a") return "alpha-channel";
    if (socket.id == "magnitude") return "magnitude";
    if (socket.id == "phase") return "phase";
    if (socket.id == "spectrum") return "complex-spectrum";
    std::string key = !socket.label.empty() ? socket.label : socket.id;
    std::transform(key.begin(), key.end(), key.begin(), [](unsigned char c) {
        return std::isalnum(c) ? static_cast<char>(std::tolower(c)) : '-';
    });
    key.erase(std::unique(key.begin(), key.end(), [](char a, char b) {
        return a == '-' && b == '-';
    }), key.end());
    while (!key.empty() && key.front() == '-') key.erase(key.begin());
    while (!key.empty() && key.back() == '-') key.pop_back();
    return key.empty() ? "unknown" : key;
}

inline Stack::NodeMath::SemanticField<Stack::NodeMath::ChannelDescriptor>
DeclaredChannelsFor(const SocketDefinition& socket) {
    using namespace Stack::NodeMath;
    const auto known = [](ChannelLayout layout, std::initializer_list<const char*> roles) {
        ChannelDescriptor descriptor;
        descriptor.layout = layout;
        for (const char* role : roles) descriptor.roles.emplace_back(role);
        return SemanticField<ChannelDescriptor>::Known(std::move(descriptor));
    };
    if (socket.id == "r") return known(ChannelLayout::Gray, { "R" });
    if (socket.id == "g") return known(ChannelLayout::Gray, { "G" });
    if (socket.id == "b") return known(ChannelLayout::Gray, { "B" });
    if (socket.id == "a") return known(ChannelLayout::Gray, { "A" });
    switch (socket.logicalType) {
        case LogicalValueType::Channel:
        case LogicalValueType::Mask:
        case LogicalValueType::ScalarField:
            return known(ChannelLayout::Gray, { "value" });
        case LogicalValueType::Vector2:
        case LogicalValueType::Vector2Field:
        case LogicalValueType::Coordinate2:
            return known(ChannelLayout::XY, { "X", "Y" });
        case LogicalValueType::Vector3:
        case LogicalValueType::Vector3Field:
            return known(ChannelLayout::NamedData, { "X", "Y", "Z" });
        case LogicalValueType::Vector4:
        case LogicalValueType::Vector4Field:
            return known(ChannelLayout::NamedData, { "X", "Y", "Z", "W" });
        case LogicalValueType::ComplexSpectrum:
            return known(ChannelLayout::ComplexPair, { "real", "imaginary" });
        case LogicalValueType::SpectrumMagnitude:
            return known(ChannelLayout::Gray, { "magnitude" });
        case LogicalValueType::SpectrumPhase:
            return known(ChannelLayout::Gray, { "phase" });
        case LogicalValueType::ColorImage:
        case LogicalValueType::DataImage:
            return SemanticField<ChannelDescriptor>::Unknown();
        default:
            return SemanticField<ChannelDescriptor>::NotApplicable();
    }
}

inline Stack::NodeMath::SemanticField<Stack::NodeMath::UnitDescriptor>
DeclaredUnitsFor(const SocketDefinition& socket) {
    using namespace Stack::NodeMath;
    if (socket.semanticRoleKey == "exposure") {
        return SemanticField<UnitDescriptor>::Known({ UnitKind::ExposureValue, {} });
    }
    if (socket.logicalType == LogicalValueType::Mask ||
        socket.semanticRoleKey == "factor") {
        return SemanticField<UnitDescriptor>::Known({ UnitKind::Unitless, {} });
    }
    switch (socket.logicalType) {
        case LogicalValueType::Scalar:
        case LogicalValueType::ScalarField:
        case LogicalValueType::Vector2:
        case LogicalValueType::Vector3:
        case LogicalValueType::Vector4:
        case LogicalValueType::Vector2Field:
        case LogicalValueType::Vector3Field:
        case LogicalValueType::Vector4Field:
        case LogicalValueType::Coordinate2:
            return SemanticField<UnitDescriptor>::Unknown();
        default:
            return SemanticField<UnitDescriptor>::NotApplicable();
    }
}

inline void NormalizeSocketDefinition(NodeKind nodeKind, SocketDefinition& socket) {
    if (socket.label.empty()) socket.label = socket.id.empty() ? "Unknown" : socket.id;
    if (socket.logicalType == Stack::NodeMath::LogicalValueType::Invalid) {
        socket.logicalType = LogicalTypeForSocketType(socket.type);
    }
    if (nodeKind == NodeKind::MagnitudePhase) {
        if (socket.id == kImageInputSocketId || socket.id == kImageOutputSocketId) {
            socket.logicalType = Stack::NodeMath::LogicalValueType::ComplexSpectrum;
        } else if (socket.id == "magnitude" || socket.id == "phase" ||
                   socket.id == kMaskOutputSocketId) {
            socket.logicalType = Stack::NodeMath::LogicalValueType::ScalarField;
        }
    } else if (nodeKind == NodeKind::SpectrumAnalyzer &&
               socket.direction == SocketDirection::Input) {
        socket.logicalType = Stack::NodeMath::LogicalValueType::ComplexSpectrum;
    }
    if (socket.semanticRoleKey.empty()) socket.semanticRoleKey = RoleKeyFromSocket(socket);
    if (socket.declaredChannels.state == Stack::NodeMath::KnowledgeState::NotApplicable) {
        socket.declaredChannels = DeclaredChannelsFor(socket);
    }
    if (socket.declaredUnits.state == Stack::NodeMath::KnowledgeState::NotApplicable) {
        socket.declaredUnits = DeclaredUnitsFor(socket);
    }
    socket.visibilityTier = !socket.visible
        ? SocketVisibilityTier::Advanced
        : (socket.optional ? SocketVisibilityTier::CommonOptional
                           : SocketVisibilityTier::Required);
}

inline const char* LogicalTypeName(Stack::NodeMath::LogicalValueType type) {
    using Type = Stack::NodeMath::LogicalValueType;
    switch (type) {
        case Type::Boolean: return "Boolean";
        case Type::Integer: return "Integer";
        case Type::Scalar: return "Scalar";
        case Type::Vector2: return "2-number vector";
        case Type::Vector3: return "3-number vector";
        case Type::Vector4: return "4-number vector";
        case Type::Matrix3: return "3 x 3 matrix";
        case Type::Matrix4: return "4 x 4 matrix";
        case Type::Coordinate2: return "2D coordinate";
        case Type::Curve1D: return "Curve";
        case Type::Lut: return "Lookup table";
        case Type::Channel: return "Channel";
        case Type::ScalarField: return "Scalar field";
        case Type::Vector2Field: return "2-component field";
        case Type::Vector3Field: return "3-component field";
        case Type::Vector4Field: return "4-component field";
        case Type::ColorImage: return "Color image";
        case Type::Mask: return "Mask";
        case Type::DataImage: return "Data image";
        case Type::ComplexSpectrum: return "Complex spectrum";
        case Type::FrequencyResponse: return "Frequency response";
        case Type::SpectrumMagnitude: return "Spectrum magnitude";
        case Type::SpectrumPhase: return "Spectrum phase";
        case Type::Histogram: return "Histogram";
        case Type::Statistics: return "Statistics";
        case Type::Metadata: return "Metadata";
        case Type::SpecializedHandle: return "Specialized handle";
        case Type::Raw: return "RAW image data";
        case Type::Analysis: return "Analysis data";
        case Type::Failure: return "Failed value";
        case Type::Invalid: return "Unknown";
    }
    return "Unknown";
}

inline std::string ChannelShapeName(
    const Stack::NodeMath::SemanticField<Stack::NodeMath::ChannelDescriptor>& channels) {
    using namespace Stack::NodeMath;
    if (channels.state == KnowledgeState::Unknown) return "Unknown channels";
    if (channels.state != KnowledgeState::Known) return {};
    const std::size_t count = channels.value.roles.size();
    std::string layout;
    switch (channels.value.layout) {
        case ChannelLayout::Gray: layout = count == 1 ? "1 channel" : "Gray"; break;
        case ChannelLayout::RGB: layout = "RGB · 3 channels"; break;
        case ChannelLayout::RGBA: layout = "RGBA · 4 channels"; break;
        case ChannelLayout::XY: layout = "2 components"; break;
        case ChannelLayout::ComplexPair: layout = "2 components"; break;
        case ChannelLayout::NamedData:
            layout = std::to_string(count) + (count == 1 ? " component" : " components");
            break;
    }
    return layout;
}

inline const char* VisibilityName(SocketVisibilityTier tier) {
    switch (tier) {
        case SocketVisibilityTier::Required: return "Required";
        case SocketVisibilityTier::CommonOptional: return "Optional";
        case SocketVisibilityTier::Advanced: return "Advanced";
    }
    return "Unknown";
}

inline std::string UnitName(
    const Stack::NodeMath::SemanticField<Stack::NodeMath::UnitDescriptor>& units) {
    using namespace Stack::NodeMath;
    if (units.state == KnowledgeState::Unknown) return "Unknown units";
    if (units.state != KnowledgeState::Known) return {};
    switch (units.value.kind) {
        case UnitKind::Unitless: return "Unitless";
        case UnitKind::ExposureValue: return "EV";
        case UnitKind::Pixels: return "Pixels";
        case UnitKind::NormalizedCoordinate: return "Normalized coordinate";
        case UnitKind::Degrees: return "Degrees";
        case UnitKind::Percent: return "Percent";
        case UnitKind::CodeValue: return "Code value";
        case UnitKind::Luminance: return "Luminance";
        case UnitKind::Custom: return units.value.customKey.empty() ? "Custom units" : units.value.customKey;
    }
    return "Unknown units";
}

inline const char* StorageName(Stack::NodeMath::LogicalValueType type) {
    using Type = Stack::NodeMath::LogicalValueType;
    switch (type) {
        case Type::Channel:
        case Type::ScalarField:
        case Type::Vector2Field:
        case Type::Vector3Field:
        case Type::Vector4Field:
        case Type::ColorImage:
        case Type::Mask:
        case Type::DataImage:
        case Type::ComplexSpectrum:
        case Type::SpectrumMagnitude:
        case Type::SpectrumPhase:
            return "Per-pixel field";
        case Type::FrequencyResponse:
            return "Resolution-independent response";
        case Type::Boolean:
        case Type::Integer:
        case Type::Scalar:
        case Type::Vector2:
        case Type::Vector3:
        case Type::Vector4:
        case Type::Matrix3:
        case Type::Matrix4:
        case Type::Coordinate2:
            return "Uniform value";
        default:
            return "Structured value";
    }
}

inline const char* PrecisionName(Stack::NodeMath::LogicalPrecision precision) {
    using Precision = Stack::NodeMath::LogicalPrecision;
    switch (precision) {
        case Precision::UInt8: return "8-bit unsigned integer";
        case Precision::UInt16: return "16-bit unsigned integer";
        case Precision::Float16: return "16-bit float";
        case Precision::Float32: return "32-bit float";
        case Precision::Float64: return "64-bit float";
    }
    return "Unknown precision";
}

inline std::string ImageStateDescription(
    const Stack::NodeMath::ValueDescriptor& descriptor) {
    using namespace Stack::NodeMath;
    std::string color = "Unknown color";
    if (descriptor.color.state == KnowledgeState::NotApplicable) {
        color = "N/A color";
    } else if (descriptor.color.state == KnowledgeState::Known) {
        if (descriptor.color.value.identity == "srgb-d65") color = "sRGB";
        else if (descriptor.color.value.identity == "display-p3-d65") color = "Display-P3";
        else color = descriptor.color.value.identity.empty()
            ? "Unknown color" : descriptor.color.value.identity;
    }

    std::string transfer = "Unknown transfer";
    if (descriptor.transfer.state == KnowledgeState::NotApplicable) {
        transfer = "N/A transfer";
    } else if (descriptor.transfer.state == KnowledgeState::Known) {
        transfer = descriptor.transfer.value.kind == TransferKind::Linear
            ? "linear" : "encoded";
    }

    std::string alpha = "Unknown alpha";
    if (descriptor.alpha.state == KnowledgeState::NotApplicable) {
        alpha = "N/A alpha";
    } else if (descriptor.alpha.state == KnowledgeState::Known) {
        switch (descriptor.alpha.value) {
            case AlphaMode::Absent: alpha = "no alpha"; break;
            case AlphaMode::Opaque: alpha = "opaque"; break;
            case AlphaMode::Straight: alpha = "straight alpha"; break;
            case AlphaMode::Premultiplied: alpha = "premultiplied alpha"; break;
        }
    }
    return color + " · " + transfer + " · " + alpha;
}

inline std::string PrimaryDescription(const SocketDefinition& socket) {
    if (socket.type == SocketType::ImageOrChannel) {
        return "Image or Channel";
    }
    std::string role;
    if (socket.semanticRoleKey == "red-channel") role = "Red channel";
    else if (socket.semanticRoleKey == "green-channel") role = "Green channel";
    else if (socket.semanticRoleKey == "blue-channel") role = "Blue channel";
    else if (socket.semanticRoleKey == "alpha-channel") role = "Alpha channel";
    else if (socket.semanticRoleKey == "exposure") role = "Exposure";
    else role = LogicalTypeName(socket.logicalType);
    const std::string shape = ChannelShapeName(socket.declaredChannels);
    if (!shape.empty()) role += " · " + shape;
    return role;
}

} // namespace EditorNodeGraph::SocketPresentation
